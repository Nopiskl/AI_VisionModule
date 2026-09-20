#include "platform/Disp2LayerAdapter.hpp"
#include "platform/LinuxFramebufferProbe.hpp"
#include "platform/SunxiFramebufferQuery.hpp"

#include <QByteArray>
#include <QFile>
#include <QTextStream>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <utility>

namespace camera {
namespace gui {
namespace {

bool parseInteger(const QString& text, int* value) {
    bool ok = false;
    const int parsed = text.trimmed().toInt(&ok, 10);
    if (ok) {
        *value = parsed;
    }
    return ok;
}

DisplayLayerContract failure(QString detail, int videoLayer = -1,
                             int configuredUiLayer = -1) {
    DisplayLayerContract result;
    result.videoLayer = videoLayer;
    result.configuredUiLayer = configuredUiLayer;
    result.detail = std::move(detail);
    return result;
}

}  // namespace

DisplayLayerContract Disp2LayerAdapter::validate(const QString& serviceConfig,
                                                 const QString& framebuffer,
                                                 const QString& backendLock,
                                                 int displayOutput) {
    QFile configFile(serviceConfig);
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return failure(QStringLiteral("无法读取 MPP 配置 %1：%2")
                           .arg(serviceConfig, configFile.errorString()));
    }

    int videoLayer = -1;
    int configuredUiLayer = -1;
    int displayX = 0, displayY = 0, displayWidth = 480, displayHeight = 800;
    QString configuredBackendLock;
    QTextStream stream(&configFile);
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        const int comment = line.indexOf(QLatin1Char('#'));
        if (comment >= 0) {
            line.truncate(comment);
        }
        const int separator = line.indexOf(QLatin1Char('='));
        if (separator < 0) {
            continue;
        }
        const QString key = line.left(separator).trimmed();
        const QString value = line.mid(separator + 1).trimmed();
        if (key == QStringLiteral("display.video_layer") &&
            !parseInteger(value, &videoLayer)) {
            return failure(QStringLiteral("display.video_layer 不是有效整数"));
        }
        if (key == QStringLiteral("display.ui_outside_layer") &&
            !parseInteger(value, &configuredUiLayer)) {
            return failure(
                QStringLiteral("display.ui_outside_layer 不是有效整数"),
                videoLayer);
        }
        int* dimension = nullptr;
        if (key == QStringLiteral("display.x")) dimension = &displayX;
        else if (key == QStringLiteral("display.y")) dimension = &displayY;
        else if (key == QStringLiteral("display.width")) dimension = &displayWidth;
        else if (key == QStringLiteral("display.height")) dimension = &displayHeight;
        if (dimension && !parseInteger(value, dimension))
            return failure(QStringLiteral("%1 不是有效整数").arg(key));
        if (key == QStringLiteral("ipc.backend_lock_path")) {
            configuredBackendLock = value;
        }
    }

    if (videoLayer < 0 || configuredUiLayer < 0) {
        return failure(
            QStringLiteral("MPP 配置缺少有效的 display.video_layer 或 "
                           "display.ui_outside_layer"),
            videoLayer, configuredUiLayer);
    }
    if (videoLayer == configuredUiLayer) {
        return failure(QStringLiteral("MPP video layer 与 Qt UI layer 配置重叠：%1")
                           .arg(videoLayer),
                       videoLayer, configuredUiLayer);
    }
    if (configuredBackendLock.isEmpty() ||
        configuredBackendLock != backendLock) {
        return failure(
            QStringLiteral("YOLO backend lock=%1，但 MPP 配置为 %2")
                .arg(backendLock,
                     configuredBackendLock.isEmpty()
                         ? QStringLiteral("<missing>")
                         : configuredBackendLock),
            videoLayer, configuredUiLayer);
    }
    if (displayOutput != 0 && displayOutput != 1) {
        return failure(QStringLiteral("DISP2 output 只接受 0 或 1"), videoLayer,
                       configuredUiLayer);
    }

    const auto fb = LinuxFramebufferProbe::inspect(framebuffer);
    if (!fb.available || displayX != 0 || displayY != 0 ||
        displayWidth != fb.width || displayHeight != fb.height) {
        return failure(
            QStringLiteral("VO 物理画布 (%1,%2,%3,%4) 与 framebuffer %5×%6 不符；"
                           "请使用 LCD 物理尺寸，不能使用旋转后的 Qt 逻辑尺寸")
                .arg(displayX).arg(displayY).arg(displayWidth).arg(displayHeight)
                .arg(fb.width).arg(fb.height),
            videoLayer, configuredUiLayer);
    }

    const QByteArray encoded = QFile::encodeName(framebuffer);
    int framebufferLayer = -1;
    std::string queryDetail;
    if (!queryFramebufferLayer(encoded.constData(), displayOutput,
                               configuredUiLayer, &framebufferLayer, &queryDetail)) {
        return failure(QStringLiteral("无法确认 %1 的 DISP2 UI layer：%2")
                           .arg(framebuffer, QString::fromStdString(queryDetail)),
                       videoLayer, configuredUiLayer);
    }

    DisplayLayerContract result;
    result.videoLayer = videoLayer;
    result.configuredUiLayer = configuredUiLayer;
    result.framebufferUiLayer = static_cast<int>(framebufferLayer);
    if (result.framebufferUiLayer != configuredUiLayer) {
        result.detail =
            QStringLiteral("linuxfb 实际 layer=%1，但 MPP outside layer 配置为 %2")
                .arg(result.framebufferUiLayer)
                .arg(configuredUiLayer);
        return result;
    }
    if (result.framebufferUiLayer == videoLayer) {
        result.detail = QStringLiteral("linuxfb 与 MPP video 使用了同一 layer=%1")
                            .arg(videoLayer);
        return result;
    }

    result.valid = true;
    result.detail = QStringLiteral("DISP2 layer 已隔离：MPP video=%1，Qt UI=%2")
                        .arg(videoLayer)
                        .arg(result.framebufferUiLayer);
    return result;
}

}  // namespace gui
}  // namespace camera
