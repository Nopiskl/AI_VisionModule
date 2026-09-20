#include "ui/MainWindow.hpp"
#include "platform/DisplayGeometry.hpp"

#include "ui/design/ui/metrics.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QList>
#include <QPixmap>
#include <QTimer>

#include <utility>

namespace camera {
namespace gui {

MainWindow::MainWindow(QString serviceExecutable, QString serviceConfig,
                       QString socketPath, QSize framebufferSize, int displayRotation,
                       bool startMppService, bool yoloAvailable,
                       QWidget* parent)
    : Dashboard(parent),
      supervisor_(this),
      ipc_(this),
      framebufferSize_(framebufferSize),
      displayRotation_(displayRotation),
      startMppService_(startMppService),
      yoloAvailable_(yoloAvailable) {
    setWindowTitle(tr("V851S AI Vision Module"));
    resize(framebufferSize_.isValid()
               ? logicalDisplaySize(framebufferSize_, displayRotation_)
               : QSize(800, 480));
    setPage(Page::Home);
    connectUiActions();
    setStatus(statusText_);
    setModeEntriesEnabled(false, true, yoloAvailable_);

    supervisor_.configure(std::move(serviceExecutable), std::move(serviceConfig));
    ipc_.setSocketPath(std::move(socketPath));

    connect(&supervisor_, &BackendSupervisor::started, &ipc_, &IpcClient::start);
    connect(&supervisor_, &BackendSupervisor::stopped, this,
            [this](int exitCode, const QString& detail) {
                if (!closing_ && !yoloSwitchPending_) {
                    emit backendDisplayReleased();
                }
                if (closing_) {
                    if (exitTimer_) exitTimer_->stop();
                    QTimer::singleShot(0, this, [this] { close(); });
                    return;
                }
                if (yoloSwitchPending_) {
                    completeYoloHandoff();
                    return;
                }
                setStatus(tr("%1（exit=%2）").arg(detail).arg(exitCode));
            });
    connect(&supervisor_, &BackendSupervisor::diagnostic, this,
            [this](const QString& text) {
                if (!text.isEmpty()) {
                    setStatus(text);
                }
            });
    connect(&ipc_, &IpcClient::diagnostic, this,
            [this](const QString& text) { setStatus(text); });
    connect(&ipc_, &IpcClient::connectedChanged, this, [this](bool connected) {
        connected_ = connected;
        if (connected) {
            setStatus(tr("MPP 服务已连接，等待选择工作模式"));
            send(QStringLiteral("hello"), QStringLiteral("hello"));
            ipc_.sendRequest(QStringLiteral("get_status"));
        } else {
            mode_ = QStringLiteral("none");
            state_ = QStringLiteral("stopped");
            snapshotBusy_ = false;
            snapshotRequestPending_ = false;
            recordPending_ = false;
            recordElapsedMs_ = 0;
            rtspAvailable_ = false;
            rtspActive_ = false;
            rtspPending_ = false;
            rtspUrl_.clear();
            uvcAvailable_ = false;
            uvcHostConnected_ = false;
            uvcStreaming_ = false;
            uvcFormat_ = QStringLiteral("none");
            uvcWidth_ = 0;
            uvcHeight_ = 0;
            playbackDesignRect_ = QRectF();
            pending_.clear();
            pendingSinceMs_.clear();
            setStatus(yoloSwitchPending_
                          ? tr("正在等待 MPP 服务释放资源…")
                          : tr("等待 MPP 服务…"));
        }
        updateControls();
    });
    connect(&ipc_, &IpcClient::responseReceived, this,
            &MainWindow::handleResponse);
    connect(&ipc_, &IpcClient::eventReceived, this, &MainWindow::handleEvent);

    statusTimer_ = new QTimer(this);
    statusTimer_->setInterval(1000);
    connect(statusTimer_, &QTimer::timeout, this, [this] {
        if (connected_) {
            ipc_.sendRequest(QStringLiteral("get_status"));
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<quint64> expired;
        for (auto iterator = pendingSinceMs_.constBegin();
             iterator != pendingSinceMs_.constEnd(); ++iterator) {
            if (now - iterator.value() >= 10000) {
                expired.push_back(iterator.key());
            }
        }
        for (quint64 requestId : expired) {
            const QString action = pending_.take(requestId);
            pendingSinceMs_.remove(requestId);
            if (action == QStringLiteral("snapshot")) {
                snapshotRequestPending_ = false;
            } else if (action == QStringLiteral("record")) {
                recordPending_ = false;
            } else if (action == QStringLiteral("rtsp")) {
                rtspPending_ = false;
            } else if (action == QStringLiteral("switch_yolo")) {
                setStatus(tr("MPP shutdown 未响应，正在终止服务以完成资源交接"));
                ipc_.stop();
                supervisor_.stop();
                completeYoloHandoff();
                continue;
            }
            showError(QStringLiteral("request_timeout"),
                      tr("后端 10 秒内未响应，正在重新查询状态"));
        }
        updateControls();
    });
    statusTimer_->start();

    yoloHandoffTimer_ = new QTimer(this);
    yoloHandoffTimer_->setSingleShot(true);
    yoloHandoffTimer_->setInterval(8000);
    connect(yoloHandoffTimer_, &QTimer::timeout, this, [this] {
        if (!yoloSwitchPending_) {
            return;
        }
        setStatus(tr("MPP 优雅退出超时，正在终止服务以完成资源交接"));
        ipc_.stop();
        supervisor_.stop();
        completeYoloHandoff();
    });

    exitTimer_ = new QTimer(this);
    exitTimer_->setSingleShot(true);
    exitTimer_->setInterval(10000);
    connect(exitTimer_, &QTimer::timeout, this, [this] {
        if (!closing_) return;
        setStatus(tr("正在等待后端释放资源…"));
        ipc_.stop();
        supervisor_.stop();
        if (!supervisor_.running()) close();
    });

    if (startMppService_) {
        supervisor_.start();
        ipc_.start();
    } else {
        setStatus(tr("显示 layer 契约无效，MPP 服务未启动"));
    }
    updateControls();
}

MainWindow::~MainWindow() {
    closing_ = true;
    ipc_.stop();
    supervisor_.stop();
}

void MainWindow::setStartupDiagnostic(const QString& text) {
    if (!text.isEmpty()) {
        setStatus(text);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!supervisor_.running()) {
        closing_ = true;
        ipc_.stop();
        event->accept();
        return;
    }
    event->ignore();
    if (closing_) return;
    closing_ = true;
    statusTimer_->stop();
    yoloHandoffTimer_->stop();
    setEnabled(false);
    setStatus(tr("正在停止后端并退出…"));
    exitTimer_->start();
    if (connected_) {
        send(QStringLiteral("exit_app"), QStringLiteral("shutdown"));
    } else {
        supervisor_.stop();
        if (!supervisor_.running()) close();
    }
}

void MainWindow::connectUiActions() {
    connect(this, &Dashboard::exitApplicationRequested, this, [this] { close(); });
    connect(this, &Dashboard::cameraPageRequested, this, [this] {
        if (connected_ && mode_ != QStringLiteral("camera")) {
            send(QStringLiteral("enter_camera"), QStringLiteral("enter_mode"),
                 {{QStringLiteral("mode"), QStringLiteral("camera")}});
        }
        updateControls();
    });
    connect(this, &Dashboard::albumPageRequested, this, [this] {
        refreshMediaLibrary();
        if (!connected_) {
            updateControls();
            return;
        }
        if (selectedMediaIndex_ >= 0 &&
            selectedMediaIndex_ < visibleMedia_.size()) {
            if (visibleMedia_[selectedMediaIndex_].photo) {
                if (mode_ != QStringLiteral("none")) {
                    send(QStringLiteral("show_photo_leave"),
                         QStringLiteral("leave_mode"));
                }
            } else {
                requestSelectedVideo(false);
            }
        } else if (mode_ != QStringLiteral("playback")) {
            send(QStringLiteral("enter_playback"), QStringLiteral("enter_mode"),
                 {{QStringLiteral("mode"), QStringLiteral("playback")}});
        }
        updateControls();
    });
    connect(this, &Dashboard::uvcPageRequested, this,
            [this] { updateControls(); });
    connect(this, &Dashboard::homePageRequested, this,
            [this] { updateControls(); });
    connect(this, &Dashboard::openCvSessionRequested, this,
            &MainWindow::requestYoloSession);
    connect(this, &Dashboard::snapshotRequested, this, [this] {
        snapshotRequestPending_ = true;
        updateControls();
        if (send(QStringLiteral("snapshot"), QStringLiteral("take_snapshot")) == 0) {
            snapshotRequestPending_ = false;
            updateControls();
        }
    });
    connect(this, &Dashboard::recordToggleRequested, this, [this] {
        recordPending_ = true;
        updateControls();
        const QString command = state_ == QStringLiteral("recording")
                                    ? QStringLiteral("stop_record")
                                    : QStringLiteral("start_record");
        if (send(QStringLiteral("record"), command) == 0) {
            recordPending_ = false;
            updateControls();
        }
    });
    connect(this, &Dashboard::rtspToggleRequested, this, [this](bool enabled) {
        rtspPending_ = true;
        updateControls();
        const QString command = enabled ? QStringLiteral("start_rtsp")
                                        : QStringLiteral("stop_rtsp");
        if (send(QStringLiteral("rtsp"), command) == 0) {
            rtspPending_ = false;
            updateControls();
        }
    });
    connect(this, &Dashboard::uvcToggleRequested, this, [this](bool enabled) {
        if (enabled) {
            send(QStringLiteral("enter_uvc"), QStringLiteral("enter_mode"),
                 {{QStringLiteral("mode"), QStringLiteral("uvc")}});
        } else {
            send(QStringLiteral("leave_uvc"), QStringLiteral("leave_mode"));
        }
        updateControls();
    });
    connect(this, &Dashboard::albumFilterRequested, this, [this](bool photos) {
        if (albumPhotos_ == photos) {
            return;
        }
        albumPhotos_ = photos;
        selectedMediaPath_.clear();
        selectedMediaIndex_ = -1;
        playbackPositionMs_ = 0;
        playbackDurationMs_ = 0;
        playbackDesignRect_ = QRectF();
        if (connected_ && mode_ == QStringLiteral("playback") &&
            state_ != QStringLiteral("idle")) {
            send(QStringLiteral("stop_playback"), QStringLiteral("stop_playback"));
        }
        rebuildVisibleMedia();
        updateControls();
    });
    connect(this, &Dashboard::albumItemRequested, this,
            &MainWindow::selectMedia);
    connect(this, &Dashboard::previousMediaRequested, this,
            [this] { selectAdjacentMedia(-1); });
    connect(this, &Dashboard::nextMediaRequested, this,
            [this] { selectAdjacentMedia(1); });
    connect(this, &Dashboard::playPauseRequested, this, [this] {
        if (selectedMediaIndex_ < 0 || selectedMediaIndex_ >= visibleMedia_.size() ||
            visibleMedia_[selectedMediaIndex_].photo) {
            return;
        }
        if (mode_ == QStringLiteral("playback") &&
            state_ == QStringLiteral("playing")) {
            send(QStringLiteral("pause"), QStringLiteral("pause"));
        } else if (mode_ == QStringLiteral("playback") &&
                   state_ == QStringLiteral("paused")) {
            send(QStringLiteral("resume"), QStringLiteral("resume"));
        } else if (mode_ == QStringLiteral("playback") &&
                   state_ == QStringLiteral("ready")) {
            send(QStringLiteral("play"), QStringLiteral("play"));
        } else {
            requestSelectedVideo(true);
        }
    });
}

quint64 MainWindow::send(const QString& action, const QString& command,
                         const QVariantMap& fields) {
    const quint64 requestId = ipc_.sendRequest(command, fields);
    if (requestId != 0) {
        pending_.insert(requestId, action);
        pendingSinceMs_.insert(requestId, QDateTime::currentMSecsSinceEpoch());
        updateControls();
    }
    return requestId;
}

void MainWindow::handleResponse(quint64 requestId, const QString&, bool ok,
                                const QString& code, const QString& detail,
                                const QVariantMap& fields) {
    const QString action = pending_.take(requestId);
    pendingSinceMs_.remove(requestId);
    applyBackendState(fields);
    if (action == QStringLiteral("snapshot")) {
        snapshotRequestPending_ = false;
    } else if (action == QStringLiteral("record")) {
        recordPending_ = false;
    } else if (action == QStringLiteral("rtsp")) {
        rtspPending_ = false;
    } else if (action == QStringLiteral("switch_yolo")) {
        if (!ok) {
            yoloSwitchPending_ = false;
            yoloHandoffTimer_->stop();
        } else {
            setStatus(tr("MPP 已停止媒体路径，正在等待进程退出…"));
            yoloHandoffTimer_->start();
        }
    }

    if (action == QStringLiteral("exit_app")) {
        // A snapshot owns a bounded worker; let it finish, then retry shutdown.
        if (!ok && code == QStringLiteral("snapshot_busy") && closing_) {
            QTimer::singleShot(150, this, [this] {
                if (closing_ && supervisor_.running() && connected_)
                    send(QStringLiteral("exit_app"), QStringLiteral("shutdown"));
            });
        }
        return;
    }
    if (!ok) {
        showError(code, detail);
    } else if (action == QStringLiteral("hello")) {
        rtspAvailable_ =
            fields.value(QStringLiteral("rtsp")).toString() == QStringLiteral("1");
        uvcAvailable_ =
            fields.value(QStringLiteral("uvc")).toString() == QStringLiteral("1");
        const QSize serviceDisplay(
            fields.value(QStringLiteral("display_width")).toInt(),
            fields.value(QStringLiteral("display_height")).toInt());
        if (serviceDisplay.isValid()) {
            serviceDisplaySize_ = serviceDisplay;
        }
        const QString configuredStorage =
            fields.value(QStringLiteral("storage_root")).toString();
        if (!configuredStorage.isEmpty()) {
            storageRoot_ = configuredStorage;
        }
        if (framebufferSize_.isValid() && serviceDisplay.isValid() &&
            serviceDisplay != framebufferSize_) {
            setStatus(tr("显示配置不匹配：linuxfb=%1×%2，MPP=%3×%4")
                          .arg(framebufferSize_.width())
                          .arg(framebufferSize_.height())
                          .arg(serviceDisplay.width())
                          .arg(serviceDisplay.height()));
        } else {
            setStatus(tr("MPP 已就绪：录像 %1×%2，显示 %3×%4，layer %5/%6")
                          .arg(fields.value(QStringLiteral("record_width")).toString())
                          .arg(fields.value(QStringLiteral("record_height")).toString())
                          .arg(fields.value(QStringLiteral("display_width")).toString())
                          .arg(fields.value(QStringLiteral("display_height")).toString())
                          .arg(fields.value(QStringLiteral("video_layer")).toString())
                          .arg(fields.value(QStringLiteral("ui_layer")).toString()));
        }
        if (currentPage() == Page::Album) {
            refreshMediaLibrary();
        }
    } else if (action == QStringLiteral("load_and_play")) {
        send(QStringLiteral("play"), QStringLiteral("play"));
    }
    updateControls();
}

void MainWindow::handleEvent(const QString& name, const QVariantMap& fields) {
    applyBackendState(fields);
    if (name == QStringLiteral("snapshot_started")) {
        snapshotRequestPending_ = false;
        snapshotBusy_ = true;
        setStatus(tr("正在拍照…"));
    } else if (name == QStringLiteral("snapshot_completed")) {
        snapshotBusy_ = false;
        setStatus(tr("照片已保存：%1")
                      .arg(fields.value(QStringLiteral("path")).toString()));
        refreshMediaLibrary();
    } else if (name == QStringLiteral("record_started")) {
        recordPending_ = false;
        setStatus(tr("正在录像：%1")
                      .arg(fields.value(QStringLiteral("path")).toString()));
    } else if (name == QStringLiteral("record_stopped")) {
        recordPending_ = false;
        setStatus(tr("录像已保存：%1")
                      .arg(fields.value(QStringLiteral("path")).toString()));
        refreshMediaLibrary();
    } else if (name == QStringLiteral("record_interrupted")) {
        recordPending_ = false;
        setStatus(tr("录像因存储异常停止 [%1] %2")
                      .arg(fields.value(QStringLiteral("code")).toString(),
                           fields.value(QStringLiteral("detail")).toString()));
        refreshMediaLibrary();
    } else if (name == QStringLiteral("rtsp_started")) {
        rtspPending_ = false;
        rtspActive_ = true;
        rtspUrl_ = fields.value(QStringLiteral("url")).toString();
        setStatus(tr("RTSP 已启动：%1").arg(rtspUrl_));
    } else if (name == QStringLiteral("rtsp_stopped")) {
        rtspPending_ = false;
        rtspActive_ = false;
        rtspUrl_.clear();
        setStatus(tr("RTSP 已停止"));
    } else if (name == QStringLiteral("rtsp_error")) {
        rtspPending_ = false;
        rtspActive_ = false;
        rtspUrl_.clear();
        showError(fields.value(QStringLiteral("code")).toString(),
                  fields.value(QStringLiteral("detail")).toString());
    } else if (name == QStringLiteral("uvc_connected") ||
               name == QStringLiteral("uvc_disconnected") ||
               name == QStringLiteral("uvc_committed") ||
               name == QStringLiteral("uvc_streaming_started") ||
               name == QStringLiteral("uvc_streaming_stopped")) {
        setStatus(uvcStatusText());
    } else if (name == QStringLiteral("uvc_error")) {
        showError(fields.value(QStringLiteral("code")).toString(),
                  fields.value(QStringLiteral("detail")).toString());
    } else if (name == QStringLiteral("media_loaded")) {
        playbackDesignRect_ = playbackDesignRect(fields);
        selectedMediaPath_ = fields.value(QStringLiteral("path")).toString();
        setStatus(tr("视频已加载：%1×%2，VO %3×%4+%5+%6")
                      .arg(fields.value(QStringLiteral("source_width")).toString())
                      .arg(fields.value(QStringLiteral("source_height")).toString())
                      .arg(fields.value(QStringLiteral("display_width")).toString())
                      .arg(fields.value(QStringLiteral("display_height")).toString())
                      .arg(fields.value(QStringLiteral("display_x")).toString())
                      .arg(fields.value(QStringLiteral("display_y")).toString()));
    } else if (name == QStringLiteral("playback_eof")) {
        setStatus(tr("视频播放完成；点击播放可重新加载"));
    } else if (name == QStringLiteral("backend_error") ||
               name == QStringLiteral("protocol_error")) {
        showError(fields.value(QStringLiteral("code")).toString(),
                  fields.value(QStringLiteral("detail")).toString());
    } else if (name == QStringLiteral("rendering_started")) {
        setStatus(fields.value(QStringLiteral("source")).toString());
    }
    updateControls();
}

void MainWindow::applyBackendState(const QVariantMap& fields) {
    if (fields.contains(QStringLiteral("mode"))) {
        mode_ = fields.value(QStringLiteral("mode")).toString();
    }
    if (fields.contains(QStringLiteral("state"))) {
        state_ = fields.value(QStringLiteral("state")).toString();
        if (state_ != QStringLiteral("recording")) {
            recordElapsedMs_ = 0;
        }
        if (state_ == QStringLiteral("recording") ||
            state_ == QStringLiteral("preview")) {
            recordPending_ = false;
        }
    }
    if (fields.contains(QStringLiteral("snapshot_busy"))) {
        snapshotBusy_ = fields.value(QStringLiteral("snapshot_busy")).toString() ==
                        QStringLiteral("1");
    }
    if (fields.contains(QStringLiteral("record_elapsed_ms"))) {
        recordElapsedMs_ =
            fields.value(QStringLiteral("record_elapsed_ms")).toLongLong();
    }
    if (fields.contains(QStringLiteral("rtsp_active"))) {
        rtspActive_ = fields.value(QStringLiteral("rtsp_active")).toString() ==
                      QStringLiteral("1");
        if (!rtspActive_) {
            rtspUrl_.clear();
        }
    }
    if (fields.contains(QStringLiteral("rtsp_url"))) {
        rtspUrl_ = fields.value(QStringLiteral("rtsp_url")).toString();
    }
    if (fields.contains(QStringLiteral("uvc_host_connected"))) {
        uvcHostConnected_ =
            fields.value(QStringLiteral("uvc_host_connected")).toString() ==
            QStringLiteral("1");
    }
    if (fields.contains(QStringLiteral("uvc_streaming"))) {
        uvcStreaming_ = fields.value(QStringLiteral("uvc_streaming")).toString() ==
                        QStringLiteral("1");
    }
    if (fields.contains(QStringLiteral("uvc_format"))) {
        uvcFormat_ = fields.value(QStringLiteral("uvc_format")).toString();
    }
    if (fields.contains(QStringLiteral("uvc_width"))) {
        uvcWidth_ = fields.value(QStringLiteral("uvc_width")).toInt();
    }
    if (fields.contains(QStringLiteral("uvc_height"))) {
        uvcHeight_ = fields.value(QStringLiteral("uvc_height")).toInt();
    }
    if (fields.contains(QStringLiteral("position_ms"))) {
        playbackPositionMs_ = fields.value(QStringLiteral("position_ms")).toLongLong();
    }
    if (fields.contains(QStringLiteral("duration_ms"))) {
        playbackDurationMs_ = fields.value(QStringLiteral("duration_ms")).toLongLong();
    }
    const QRectF reportedRect = playbackDesignRect(fields);
    if (reportedRect.isValid()) {
        playbackDesignRect_ = reportedRect;
    }
}

QString MainWindow::uvcStatusText() const {
    if (mode_ != QStringLiteral("uvc")) {
        return tr("点击下方按钮启用 UVC 输出\n启用后可作为 USB 摄像头使用");
    }
    QString profile = tr("尚未协商格式");
    if (uvcWidth_ > 0 && uvcHeight_ > 0 && uvcFormat_ != QStringLiteral("none")) {
        profile = tr("%1 %2×%3 @30 fps")
                      .arg(uvcFormat_.toUpper())
                      .arg(uvcWidth_)
                      .arg(uvcHeight_);
    }
    if (state_ == QStringLiteral("uvc_waiting_host")) {
        return tr("UVC 已就绪，等待 PC Host 连接");
    }
    if (state_ == QStringLiteral("uvc_connected")) {
        return tr("Host 已连接，等待 PROBE/COMMIT");
    }
    if (state_ == QStringLiteral("uvc_committed")) {
        return tr("已协商 %1，等待 Host STREAMON").arg(profile);
    }
    if (state_ == QStringLiteral("uvc_streaming") || uvcStreaming_) {
        return tr("正在向 Host 输出：%1").arg(profile);
    }
    if (state_ == QStringLiteral("uvc_error")) {
        return tr("UVC 路径发生错误，请停止后重试");
    }
    return uvcHostConnected_ ? tr("Host 已连接：%1").arg(profile)
                             : tr("UVC 已启用，等待 Host：%1").arg(profile);
}

void MainWindow::updateControls() {
    const auto actionPending = [this](const QString& action) {
        return pending_.values().contains(action);
    };
    const bool modePending = actionPending(QStringLiteral("enter_camera")) ||
                             actionPending(QStringLiteral("enter_playback")) ||
                             actionPending(QStringLiteral("enter_uvc")) ||
                             actionPending(QStringLiteral("leave_uvc")) ||
                             actionPending(QStringLiteral("show_photo_leave")) ||
                             actionPending(QStringLiteral("load")) ||
                             actionPending(QStringLiteral("load_and_play"));
    const bool snapshotTransition = snapshotBusy_ || snapshotRequestPending_;
    const bool camera = connected_ && mode_ == QStringLiteral("camera");
    const bool preview = camera && state_ == QStringLiteral("preview");
    const bool recording = camera && state_ == QStringLiteral("recording");

    QString rtspStatus;
    if (!rtspAvailable_) {
        rtspStatus = tr("RTSP 功能不可用");
    } else if (rtspPending_) {
        rtspStatus = rtspActive_ ? tr("正在停止 RTSP…") : tr("正在启动 RTSP…");
    } else if (rtspActive_) {
        rtspStatus = rtspUrl_.isEmpty() ? tr("RTSP 已启用，正在查询地址…")
                                        : tr("RTSP：%1").arg(rtspUrl_);
    } else {
        rtspStatus = tr("RTSP 未启用");
    }
    setCameraUiState(preview && !snapshotTransition && !recording &&
                         !recordPending_ && !modePending,
                     snapshotTransition,
                     (recording || (preview && !snapshotTransition)) &&
                         !recordPending_ && !modePending,
                     recording, recordPending_, camera && rtspAvailable_ &&
                         !recordPending_ && !modePending,
                     rtspActive_, rtspPending_, rtspStatus,
                     formatTime(recordElapsedMs_));

    const bool uvcMode = connected_ && mode_ == QStringLiteral("uvc");
    setUvcUiState(uvcMode,
                  connected_ && uvcAvailable_ && !modePending &&
                      !snapshotTransition && !recordPending_,
                  uvcStatusText());

    QString modeSummary;
    if (!connected_) {
        modeSummary = tr("后端未连接；OpenCV 会话仍可按可用性独立启动");
    } else if (mode_ == QStringLiteral("camera")) {
        QStringList states;
        states << (recording ? tr("正在录像") : tr("实时预览"));
        if (rtspActive_) {
            states << tr("RTSP 已启用");
        }
        modeSummary = tr("当前 Camera 后端仍活动：%1").arg(states.join(QStringLiteral(" / ")));
    } else if (mode_ == QStringLiteral("playback")) {
        modeSummary = tr("当前 Playback 后端仍活动：%1").arg(state_);
    } else if (mode_ == QStringLiteral("uvc")) {
        modeSummary = tr("当前 UVC 后端仍活动：%1").arg(uvcStatusText().section('\n', 0, 0));
    } else {
        modeSummary = tr("当前无活动后端；同一时间仅可启用一种工作模式");
    }
    setHomeModeSummary(modeSummary);
    setModeEntriesEnabled(connected_ && !yoloSwitchPending_ && !modePending &&
                              !snapshotTransition && !recordPending_,
                          !yoloSwitchPending_ && !modePending,
                          yoloAvailable_ && !yoloSwitchPending_ &&
                              !snapshotTransition && !recordPending_);
    setNavigationEnabled(connected_ && !yoloSwitchPending_ && !modePending &&
                         !snapshotTransition && !recordPending_);

    const bool playbackActionPending = actionPending(QStringLiteral("load")) ||
                                       actionPending(QStringLiteral("load_and_play")) ||
                                       actionPending(QStringLiteral("play")) ||
                                       actionPending(QStringLiteral("pause")) ||
                                       actionPending(QStringLiteral("resume")) ||
                                       actionPending(QStringLiteral("stop_playback"));
    const bool selected = selectedMediaIndex_ >= 0 &&
                          selectedMediaIndex_ < visibleMedia_.size();
    const bool selectedPhoto = selected && visibleMedia_[selectedMediaIndex_].photo;
    const bool selectedVideo = selected && !selectedPhoto;
    const bool interactionEnabled = !modePending && !playbackActionPending;
    const bool navigationEnabled = selected && interactionEnabled &&
                                   (albumPhotos_ || connected_);
    const bool playEnabled = connected_ && selectedVideo && !modePending &&
                             !playbackActionPending;
    setAlbumPlaybackState(interactionEnabled, navigationEnabled, playEnabled,
                          mode_ == QStringLiteral("playback") &&
                              state_ == QStringLiteral("playing"),
                          tr("%1 / %2")
                              .arg(formatTime(playbackPositionMs_),
                                   formatTime(playbackDurationMs_)));
    if (selectedVideo) {
        if (mode_ == QStringLiteral("playback") &&
            state_ != QStringLiteral("idle") && playbackDesignRect_.isValid()) {
            setAlbumVideo(playbackDesignRect_, visibleMedia_[selectedMediaIndex_].label);
        } else {
            setAlbumEmpty(playbackActionPending ? tr("正在加载视频…")
                                                : tr("已选择视频，点击播放"));
        }
    } else if (!selectedPhoto && visibleMedia_.isEmpty()) {
        setAlbumEmpty(albumPhotos_ ? tr("媒体库中没有照片")
                                   : tr("媒体库中没有录像"));
    }
}

void MainWindow::setStatus(const QString& text) {
    statusText_ = text;
    setStatusMessage(statusText_);
}

void MainWindow::requestYoloSession() {
    if (!yoloAvailable_ || yoloSwitchPending_) {
        return;
    }
    yoloSwitchPending_ = true;
    setStatus(tr("正在停止 MPP 并交接摄像头/framebuffer…"));
    updateControls();

    if (!connected_) {
        ipc_.stop();
        supervisor_.stop();
        completeYoloHandoff();
        return;
    }
    if (send(QStringLiteral("switch_yolo"), QStringLiteral("shutdown")) == 0) {
        ipc_.stop();
        supervisor_.stop();
        completeYoloHandoff();
    }
}

void MainWindow::completeYoloHandoff() {
    if (!yoloSwitchPending_ || yoloExitScheduled_ || supervisor_.running()) {
        return;
    }
    yoloExitScheduled_ = true;
    yoloHandoffTimer_->stop();
    ipc_.stop();
    QTimer::singleShot(0, this, [this] { emit yoloSessionRequested(); });
}

void MainWindow::showError(const QString& code, const QString& detail) {
    if (code == QStringLiteral("camera_media_unavailable")) {
        qWarning("Media capture unavailable: %s", qPrintable(detail));
        setStatus(tr("媒体采集未就绪，当前仍可预览"));
        return;
    }
    setStatus(tr("后端错误 [%1] %2").arg(code, detail));
}

void MainWindow::refreshMediaLibrary() {
    const QString previousSelection = selectedMediaPath_;
    allMedia_.clear();
    const QDir directory(storageRoot_);
    const QFileInfoList entries = directory.entryInfoList(
        QDir::Files | QDir::Readable | QDir::NoSymLinks, QDir::Time);
    constexpr int kMaximumMediaEntries = 512;
    for (const QFileInfo& info : entries) {
        const QString suffix = info.suffix().toLower();
        const bool photo = suffix == QStringLiteral("jpg") ||
                           suffix == QStringLiteral("jpeg");
        const bool video = suffix == QStringLiteral("mp4");
        if (!photo && !video) {
            continue;
        }
        allMedia_.push_back(
            {info.absoluteFilePath(), info.fileName(), photo,
             info.lastModified().toMSecsSinceEpoch()});
        if (allMedia_.size() >= kMaximumMediaEntries) {
            break;
        }
    }
    selectedMediaPath_ = previousSelection;
    rebuildVisibleMedia();
}

void MainWindow::rebuildVisibleMedia() {
    visibleMedia_.clear();
    QStringList labels;
    int restoredIndex = -1;
    for (const MediaEntry& entry : allMedia_) {
        if (entry.photo != albumPhotos_) {
            continue;
        }
        if (entry.path == selectedMediaPath_) {
            restoredIndex = visibleMedia_.size();
        }
        visibleMedia_.push_back(entry);
        labels.push_back(entry.label);
    }
    selectedMediaIndex_ = restoredIndex;
    if (restoredIndex < 0) {
        selectedMediaPath_.clear();
        playbackPositionMs_ = 0;
        playbackDurationMs_ = 0;
        playbackDesignRect_ = QRectF();
        setAlbumEmpty(visibleMedia_.isEmpty()
                          ? (albumPhotos_ ? tr("媒体库中没有照片")
                                          : tr("媒体库中没有录像"))
                          : tr("请选择图片/视频"));
    }
    setAlbumItems(labels, restoredIndex);
}

void MainWindow::selectMedia(int index) {
    if (index < 0 || index >= visibleMedia_.size()) {
        selectedMediaIndex_ = -1;
        selectedMediaPath_.clear();
        playbackDesignRect_ = QRectF();
        setAlbumEmpty(tr("请选择图片/视频"));
        updateControls();
        return;
    }
    selectedMediaIndex_ = index;
    const MediaEntry& entry = visibleMedia_[index];
    selectedMediaPath_ = entry.path;
    playbackPositionMs_ = 0;
    playbackDurationMs_ = 0;
    playbackDesignRect_ = QRectF();
    if (entry.photo) {
        if (connected_ && mode_ != QStringLiteral("none")) {
            send(QStringLiteral("show_photo_leave"), QStringLiteral("leave_mode"));
        }
        const QPixmap photo(entry.path);
        if (photo.isNull()) {
            setAlbumEmpty(tr("图片读取失败：%1").arg(entry.label));
            showError(QStringLiteral("image_load_failed"), entry.path);
        } else {
            setAlbumPhoto(photo, entry.label);
            setStatus(tr("照片：%1").arg(entry.path));
        }
    } else {
        setAlbumEmpty(tr("正在加载视频…"));
        requestSelectedVideo(false);
    }
    updateControls();
}

void MainWindow::selectAdjacentMedia(int offset) {
    const int next = selectedMediaIndex_ + offset;
    if (next < 0 || next >= visibleMedia_.size()) {
        return;
    }
    QStringList labels;
    labels.reserve(visibleMedia_.size());
    for (const MediaEntry& entry : visibleMedia_) {
        labels.push_back(entry.label);
    }
    setAlbumItems(labels, next);
    selectMedia(next);
}

void MainWindow::requestSelectedVideo(bool playAfterLoad) {
    if (!connected_ || selectedMediaIndex_ < 0 ||
        selectedMediaIndex_ >= visibleMedia_.size() ||
        visibleMedia_[selectedMediaIndex_].photo) {
        return;
    }
    playbackDesignRect_ = QRectF();
    playbackPositionMs_ = 0;
    playbackDurationMs_ = 0;
    setAlbumEmpty(tr("正在加载视频…"));
    send(playAfterLoad ? QStringLiteral("load_and_play") : QStringLiteral("load"),
         QStringLiteral("load_media"),
         {{QStringLiteral("path"), visibleMedia_[selectedMediaIndex_].path}});
}

QRectF MainWindow::playbackDesignRect(const QVariantMap& fields) const {
    if (!fields.contains(QStringLiteral("display_x")) ||
        !fields.contains(QStringLiteral("display_y")) ||
        !fields.contains(QStringLiteral("display_width")) ||
        !fields.contains(QStringLiteral("display_height")) ||
        !serviceDisplaySize_.isValid()) {
        return QRectF();
    }
    const QRectF physical(fields.value(QStringLiteral("display_x")).toInt(),
                          fields.value(QStringLiteral("display_y")).toInt(),
                          fields.value(QStringLiteral("display_width")).toInt(),
                          fields.value(QStringLiteral("display_height")).toInt());
    const QRectF logical = physicalToLogicalRect(
        physical, serviceDisplaySize_, displayRotation_);
    if (!logical.isValid())
        return {};
    const QSize canvas = logicalDisplaySize(serviceDisplaySize_, displayRotation_);
    const qreal scaleX = Ui::Metrics::DesignWidth / canvas.width();
    const qreal scaleY = Ui::Metrics::DesignHeight / canvas.height();
    return {logical.x() * scaleX, logical.y() * scaleY,
            logical.width() * scaleX, logical.height() * scaleY};
}

QString MainWindow::formatTime(qint64 milliseconds) {
    if (milliseconds < 0) {
        return QStringLiteral("--:--");
    }
    const qint64 seconds = milliseconds / 1000;
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

}  // namespace gui
}  // namespace camera
