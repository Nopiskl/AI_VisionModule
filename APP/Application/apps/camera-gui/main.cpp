#include "platform/Disp2LayerAdapter.hpp"
#include "platform/DisplayGeometry.hpp"
#include "platform/FramebufferSession.hpp"
#include "camera/common/BackendLock.hpp"
#include "platform/LinuxFramebufferProbe.hpp"
#include "ui/MainWindow.hpp"

#include <QApplication>
#include <QByteArray>
#include <QDebug>
#include <QFileInfo>
#include <QSize>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr int kSwitchToYoloExitCode = 42;

struct Options {
    QString service{QStringLiteral("/usr/bin/camera-mpp-service")};
    QString config{QStringLiteral("/etc/v851s-camera/mpp-service.conf")};
    QString socket{QStringLiteral("/run/v851s-camera/mpp.sock")};
    QString backendLock{QStringLiteral("/run/v851s-camera/backend.lock")};
    QString framebuffer{QStringLiteral("/dev/fb0")};
    QString yoloExecutable{
        QStringLiteral("/usr/libexec/v851s-camera/camera-yolo-worker")};
    QString yoloModel{QStringLiteral("/etc/v851s-camera/yolov8.nb")};
    QString yoloClasses{
        QStringLiteral("/etc/v851s-camera/yolov8-classes.txt")};
    unsigned int yoloMemoryBytes{17U * 1024U * 1024U};
    int yoloCameraIndex{0};
    int displayOutput{0};
    bool windowed{false};
    bool uiSession{false};
    bool help{false};
};

volatile sig_atomic_t gChildPid = -1;
volatile sig_atomic_t gChildIsYolo = 0;
volatile sig_atomic_t gStopSignal = 0;
volatile sig_atomic_t gReturnToUi = 0;
volatile sig_atomic_t gUiSignalWriteDescriptor = -1;

void forwardStop(int signalNumber) {
    gStopSignal = signalNumber;
    const pid_t child = static_cast<pid_t>(gChildPid);
    if (child > 0) {
        kill(child, signalNumber);
    }
}

void returnToUi(int) {
    if (gChildIsYolo == 0) {
        return;
    }
    const pid_t child = static_cast<pid_t>(gChildPid);
    const int signalNumber = gReturnToUi == 0 ? SIGTERM : SIGKILL;
    gReturnToUi = 1;
    if (child > 0) {
        kill(child, signalNumber);
    }
}

void requestUiStop(int) {
    const int descriptor = static_cast<int>(gUiSignalWriteDescriptor);
    if (descriptor < 0) {
        return;
    }
    const unsigned char marker = 1;
    const int savedError = errno;
    const ssize_t ignored = ::write(descriptor, &marker, sizeof(marker));
    (void)ignored;
    errno = savedError;
}

bool prepareDescriptor(int descriptor) {
    const int statusFlags = ::fcntl(descriptor, F_GETFL, 0);
    const int descriptorFlags = ::fcntl(descriptor, F_GETFD, 0);
    return statusFlags >= 0 && descriptorFlags >= 0 &&
           ::fcntl(descriptor, F_SETFL, statusFlags | O_NONBLOCK) == 0 &&
           ::fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0;
}

void drainDescriptor(int descriptor) {
    unsigned char buffer[32];
    while (::read(descriptor, buffer, sizeof(buffer)) > 0) {
    }
}

void printUsage(const char* program) {
    qInfo().noquote()
        << QStringLiteral(
               "Usage: %1 [--service PATH] [--service-config PATH] "
               "[--socket PATH]\n"
               "          [--backend-lock PATH] [--framebuffer PATH] "
               "[--display-output 0|1]\n"
               "          [--yolo PATH] [--yolo-model PATH] "
               "[--yolo-classes PATH]\n"
               "          [--yolo-memory BYTES] [--yolo-camera INDEX] "
               "[--windowed]")
               .arg(QString::fromLocal8Bit(program));
}

bool parseUnsigned(const QString& value, unsigned int* output) {
    bool ok = false;
    const qulonglong parsed = value.toULongLong(&ok, 10);
    if (!ok || parsed == 0 || parsed > 0xffffffffULL) {
        return false;
    }
    *output = static_cast<unsigned int>(parsed);
    return true;
}

bool parseNonNegative(const QString& value, int* output) {
    bool ok = false;
    const int parsed = value.toInt(&ok, 10);
    if (!ok || parsed < 0) {
        return false;
    }
    *output = parsed;
    return true;
}

bool parseOptions(int argc, char** argv, Options* options) {
    for (int index = 1; index < argc; ++index) {
        const QString argument = QString::fromLocal8Bit(argv[index]);
        if (argument == QStringLiteral("--windowed")) {
            options->windowed = true;
        } else if (argument == QStringLiteral("--ui-session")) {
            options->uiSession = true;
        } else if (argument == QStringLiteral("--help")) {
            options->help = true;
        } else if (index + 1 < argc) {
            const QString value = QString::fromLocal8Bit(argv[++index]);
            if (argument == QStringLiteral("--service")) {
                options->service = value;
            } else if (argument == QStringLiteral("--service-config")) {
                options->config = value;
            } else if (argument == QStringLiteral("--socket")) {
                options->socket = value;
            } else if (argument == QStringLiteral("--backend-lock")) {
                options->backendLock = value;
            } else if (argument == QStringLiteral("--framebuffer")) {
                options->framebuffer = value;
            } else if (argument == QStringLiteral("--yolo")) {
                options->yoloExecutable = value;
            } else if (argument == QStringLiteral("--yolo-model")) {
                options->yoloModel = value;
            } else if (argument == QStringLiteral("--yolo-classes")) {
                options->yoloClasses = value;
            } else if (argument == QStringLiteral("--yolo-memory")) {
                if (!parseUnsigned(value, &options->yoloMemoryBytes)) {
                    return false;
                }
            } else if (argument == QStringLiteral("--yolo-camera")) {
                if (!parseNonNegative(value, &options->yoloCameraIndex)) {
                    return false;
                }
            } else if (argument == QStringLiteral("--display-output")) {
                if (!parseNonNegative(value, &options->displayOutput) ||
                    options->displayOutput > 1) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool yoloFilesAvailable(const Options& options, QString* detail) {
    const QFileInfo executable(options.yoloExecutable);
    const QFileInfo model(options.yoloModel);
    const QFileInfo classes(options.yoloClasses);
    if (!executable.isFile() || !executable.isExecutable()) {
        *detail = QObject::tr("YOLO worker 不可执行：%1")
                      .arg(options.yoloExecutable);
        return false;
    }
    if (!model.isFile() || !model.isReadable()) {
        *detail = QObject::tr("YOLO 模型不可读：%1").arg(options.yoloModel);
        return false;
    }
    if (!classes.isFile() || !classes.isReadable()) {
        *detail = QObject::tr("YOLO 类别文件不可读：%1")
                      .arg(options.yoloClasses);
        return false;
    }
    detail->clear();
    return true;
}

int runUiSession(int argc, char** argv, const Options& options) {
    if (!options.windowed && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM",
                QStringLiteral("linuxfb:fb=%1")
                    .arg(options.framebuffer)
                    .toLocal8Bit());
    }

    // This guard outlives QApplication. Its destructor runs only after all
    // QWidget/QPA drawing and the owned MPP process have stopped.
    camera::gui::FramebufferSession framebufferSession(
        options.framebuffer, !options.windowed);

    // The current BSP powers down the LCD on every /dev/disp close,
    // including a read-only query fd. Complete probing before linuxfb opens
    // the framebuffer and enables the panel; never probe /dev/disp after
    // QApplication has acquired the display.
    const camera::gui::FramebufferInfo framebuffer =
        camera::gui::LinuxFramebufferProbe::inspect(options.framebuffer);
    const QSize framebufferSize = framebuffer.available
                                      ? QSize(framebuffer.width, framebuffer.height)
                                      : QSize();

    camera::gui::DisplayLayerContract layerContract;
    if (options.windowed) {
        layerContract.valid = true;
        layerContract.detail =
            QObject::tr("windowed 诊断模式跳过 DISP2 layer 门禁");
    } else {
        layerContract = camera::gui::Disp2LayerAdapter::validate(
            options.config, options.framebuffer, options.backendLock,
            options.displayOutput);
    }

    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("camera-gui"));

    int signalDescriptors[2] = {-1, -1};
    if (::pipe(signalDescriptors) != 0 ||
        !prepareDescriptor(signalDescriptors[0]) ||
        !prepareDescriptor(signalDescriptors[1])) {
        qCritical("GUI signal pipe failed: %s", std::strerror(errno));
        if (signalDescriptors[0] >= 0) {
            ::close(signalDescriptors[0]);
        }
        if (signalDescriptors[1] >= 0) {
            ::close(signalDescriptors[1]);
        }
        return 1;
    }
    gUiSignalWriteDescriptor = signalDescriptors[1];
    std::signal(SIGINT, requestUiStop);
    std::signal(SIGTERM, requestUiStop);

    QString yoloDiagnostic;
    const bool yoloAvailable = yoloFilesAvailable(options, &yoloDiagnostic);
    camera::gui::MainWindow window(
        options.service, options.config, options.socket, framebufferSize,
        options.windowed ? 0 : camera::gui::linuxfbRotation(
            QString::fromLocal8Bit(qgetenv("QT_QPA_PLATFORM"))),
        layerContract.valid, yoloAvailable);

    QObject::connect(&window, &camera::gui::MainWindow::backendDisplayReleased,
                     &application, [&window, &options] {
        if (options.windowed)
            return;
        // MPP SYS Exit closes /dev/disp, whose BSP release callback disables
        // the panel globally. Only the still-active Qt owner restores it:
        // sunxi_fb_open re-enables LCD and its framebuffer configuration.
        const auto restored =
            camera::gui::LinuxFramebufferProbe::inspect(options.framebuffer);
        if (!restored.available)
            window.setStartupDiagnostic(QObject::tr("显示恢复失败：%1")
                                            .arg(restored.detail));
        window.update();
    });

    QStringList diagnostics;
    const QByteArray previousDiagnostic = qgetenv("CAMERA_GUI_SESSION_DIAGNOSTIC");
    if (!previousDiagnostic.isEmpty()) {
        diagnostics.push_back(QString::fromLocal8Bit(previousDiagnostic));
    }
    if (!framebuffer.available && !options.windowed) {
        diagnostics.push_back(
            QObject::tr("无法只读探测 %1：%2")
                .arg(options.framebuffer, framebuffer.detail));
    } else if (framebuffer.available && framebuffer.alphaBits == 0 &&
               !options.windowed) {
        diagnostics.push_back(
            QObject::tr("UI framebuffer 没有 alpha 位：%1")
                .arg(framebuffer.detail));
    }
    diagnostics.push_back(layerContract.detail);
    if (!yoloAvailable) {
        diagnostics.push_back(yoloDiagnostic);
    }
    window.setStartupDiagnostic(diagnostics.join(QLatin1Char('\n')));

    QObject::connect(&window, &camera::gui::MainWindow::yoloSessionRequested,
                     &application,
                     [&application] { application.exit(kSwitchToYoloExitCode); });
    const int signalReadDescriptor = signalDescriptors[0];
    QSocketNotifier signalNotifier(signalReadDescriptor, QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &application,
                     [&window, signalReadDescriptor](int) {
                         drainDescriptor(signalReadDescriptor);
                         window.close();
                     });

    if (options.windowed) {
        window.show();
    } else {
        window.showFullScreen();
    }
    const int result = application.exec();
    signalNotifier.setEnabled(false);
    gUiSignalWriteDescriptor = -1;
    ::close(signalDescriptors[0]);
    ::close(signalDescriptors[1]);
    return result;
}

struct ChildResult {
    int exitCode{1};
    bool signalled{false};
    int signalNumber{0};
};

ChildResult runChild(const std::vector<std::string>& arguments, bool yolo,
                     const std::string& diagnostic = {}) {
    const pid_t child = fork();
    if (child < 0) {
        qCritical("fork failed: %s", std::strerror(errno));
        return {};
    }
    if (child == 0) {
        if (!diagnostic.empty()) {
            setenv("CAMERA_GUI_SESSION_DIAGNOSTIC", diagnostic.c_str(), 1);
        } else {
            unsetenv("CAMERA_GUI_SESSION_DIAGNOSTIC");
        }
        std::vector<char*> childArguments;
        childArguments.reserve(arguments.size() + 1);
        for (const std::string& argument : arguments) {
            childArguments.push_back(const_cast<char*>(argument.c_str()));
        }
        childArguments.push_back(nullptr);
        execvp(childArguments.front(), childArguments.data());
        std::fprintf(stderr, "exec %s failed: %s\n", childArguments.front(),
                     std::strerror(errno));
        _exit(127);
    }

    gChildPid = child;
    gChildIsYolo = yolo ? 1 : 0;
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            qCritical("waitpid failed: %s", std::strerror(errno));
            status = 1 << 8;
            break;
        }
    }
    gChildPid = -1;
    gChildIsYolo = 0;

    ChildResult result;
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.signalled = true;
        result.signalNumber = WTERMSIG(status);
        result.exitCode = 128 + result.signalNumber;
    }
    return result;
}

std::vector<std::string> uiArguments(int argc, char** argv) {
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(argc) + 1);
    for (int index = 0; index < argc; ++index) {
        if (std::strcmp(argv[index], "--ui-session") != 0) {
            result.emplace_back(argv[index]);
        }
    }
    result.emplace_back("--ui-session");
    return result;
}

std::vector<std::string> yoloArguments(const Options& options) {
    return {
        options.yoloExecutable.toLocal8Bit().constData(),
        "--model",
        options.yoloModel.toLocal8Bit().constData(),
        "--classes",
        options.yoloClasses.toLocal8Bit().constData(),
        "--memory",
        std::to_string(options.yoloMemoryBytes),
        "--camera",
        std::to_string(options.yoloCameraIndex),
        "--framebuffer",
        options.framebuffer.toLocal8Bit().constData(),
        "--backend-lock",
        options.backendLock.toLocal8Bit().constData(),
    };
}

int runSessionSupervisor(int argc, char** argv, const Options& options) {
    std::signal(SIGINT, forwardStop);
    std::signal(SIGTERM, forwardStop);
    std::signal(SIGUSR1, returnToUi);

    const std::vector<std::string> ui = uiArguments(argc, argv);
    std::string diagnostic;
    for (;;) {
        const ChildResult uiResult = runChild(ui, false, diagnostic);
        diagnostic.clear();
        if (uiResult.signalled && !options.windowed) {
            camera::common::BackendLock cleanupLock;
            bool acquired = false;
            for (int attempt = 0; attempt < 40; ++attempt) {
                const auto status = cleanupLock.acquire(
                    options.backendLock.toLocal8Bit().constData(), "gui-display-cleanup");
                if (status.ok) { acquired = true; break; }
                if (status.code != "backend_busy") break;
                ::usleep(200000);
            }
            if (acquired)
                camera::gui::FramebufferSession::release(options.framebuffer);
            else
                qWarning("UI crash cleanup deferred: backend still owns resources");
        }
        if (gStopSignal != 0) {
            return 128 + static_cast<int>(gStopSignal);
        }
        if (uiResult.exitCode != kSwitchToYoloExitCode) {
            return uiResult.exitCode;
        }

        gReturnToUi = 0;
        const ChildResult yoloResult = runChild(yoloArguments(options), true);
        if (gStopSignal != 0) {
            return 128 + static_cast<int>(gStopSignal);
        }
        if (gReturnToUi != 0) {
            diagnostic = "YOLO 已停止，Qt 已重新接管 framebuffer";
        } else if (yoloResult.exitCode == 0) {
            diagnostic = "YOLO 已退出，Qt 已重新接管 framebuffer";
        } else if (yoloResult.signalled) {
            diagnostic = "YOLO 被信号 " +
                         std::to_string(yoloResult.signalNumber) +
                         " 终止，Qt 已重新接管 framebuffer";
        } else {
            diagnostic = "YOLO 启动或运行失败（exit=" +
                         std::to_string(yoloResult.exitCode) +
                         "），Qt 已重新接管 framebuffer";
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseOptions(argc, argv, &options)) {
        printUsage(argv[0]);
        return 2;
    }
    if (options.help) {
        printUsage(argv[0]);
        return 0;
    }
    return options.uiSession ? runUiSession(argc, argv, options)
                             : runSessionSupervisor(argc, argv, options);
}
