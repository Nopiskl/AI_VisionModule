#include "backend/BackendSupervisor.hpp"

#include <utility>
#include <QDebug>

namespace camera {
namespace gui {

BackendSupervisor::BackendSupervisor(QObject* parent) : QObject(parent) {
    // Preserve vendor output in the supervisor log; user-facing state and
    // errors arrive over IPC. Raw ANSI log chunks are not UI status messages.
    process_.setProcessChannelMode(QProcess::ForwardedChannels);
    connect(&process_, &QProcess::started, this, &BackendSupervisor::started);
    connect(&process_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
                const QString reason =
                    status == QProcess::CrashExit ? tr("MPP 服务异常退出")
                                                  : tr("MPP 服务已退出");
                qInfo().nospace() << "MPP backend exited: code=" << exitCode
                                  << " crash=" << (status == QProcess::CrashExit);
                emit stopped(exitCode, reason);
            });
    connect(&process_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) { emit diagnostic(process_.errorString()); });
}

void BackendSupervisor::configure(QString executable, QString configPath) {
    executable_ = std::move(executable);
    configPath_ = std::move(configPath);
}

void BackendSupervisor::start() {
    if (running()) {
        return;
    }
    if (executable_.isEmpty()) {
        emit diagnostic(tr("MPP 服务程序路径为空"));
        return;
    }
    process_.setProgram(executable_);
    process_.setArguments({QStringLiteral("--config"), configPath_,
                           QStringLiteral("--exit-with-parent")});
    process_.start();
}

void BackendSupervisor::stop() {
    if (!running()) {
        return;
    }
    process_.terminate();
    if (!process_.waitForFinished(7000)) {
        emit diagnostic(tr("MPP 服务停止超时，发送强制终止"));
        process_.kill();
        process_.waitForFinished(1000);
    }
}

bool BackendSupervisor::running() const {
    return process_.state() != QProcess::NotRunning;
}

}  // namespace gui
}  // namespace camera
