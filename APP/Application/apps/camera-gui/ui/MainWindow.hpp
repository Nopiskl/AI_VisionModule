#pragma once

#include "backend/BackendSupervisor.hpp"
#include "backend/IpcClient.hpp"
#include "ui/design/dashboard.h"

#include <QHash>
#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QVector>

class QCloseEvent;
class QTimer;

namespace camera {
namespace gui {

class MainWindow final : public Dashboard {
    Q_OBJECT

public:
    MainWindow(QString serviceExecutable, QString serviceConfig,
               QString socketPath, QSize framebufferSize, int displayRotation,
               bool startMppService, bool yoloAvailable,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    void setStartupDiagnostic(const QString& text);

signals:
    void yoloSessionRequested();
    void backendDisplayReleased();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    bool closing_{false};
    struct MediaEntry {
        QString path;
        QString label;
        bool photo{false};
        qint64 modifiedMs{0};
    };

    void connectUiActions();
    quint64 send(const QString& action, const QString& command,
                 const QVariantMap& fields = QVariantMap{});
    void handleResponse(quint64 requestId, const QString& name, bool ok,
                        const QString& code, const QString& detail,
                        const QVariantMap& fields);
    void handleEvent(const QString& name, const QVariantMap& fields);
    void applyBackendState(const QVariantMap& fields);
    void updateControls();
    QString uvcStatusText() const;
    void setStatus(const QString& text);
    void requestYoloSession();
    void completeYoloHandoff();
    void showError(const QString& code, const QString& detail);
    void refreshMediaLibrary();
    void rebuildVisibleMedia();
    void selectMedia(int index);
    void selectAdjacentMedia(int offset);
    void requestSelectedVideo(bool playAfterLoad);
    QRectF playbackDesignRect(const QVariantMap& fields) const;
    static QString formatTime(qint64 milliseconds);

    BackendSupervisor supervisor_;
    IpcClient ipc_;
    QHash<quint64, QString> pending_;
    QHash<quint64, qint64> pendingSinceMs_;
    QTimer* statusTimer_{nullptr};
    QTimer* yoloHandoffTimer_{nullptr};
    QTimer* exitTimer_{nullptr};
    QVector<MediaEntry> allMedia_;
    QVector<MediaEntry> visibleMedia_;
    QString mode_{QStringLiteral("none")};
    QString state_{QStringLiteral("stopped")};
    QString statusText_{QStringLiteral("正在启动 MPP 服务…")};
    QString rtspUrl_;
    QString storageRoot_{QStringLiteral("/mnt/extsd/v851s-camera")};
    QString selectedMediaPath_;
    QRectF playbackDesignRect_;
    QSize framebufferSize_;
    QSize serviceDisplaySize_;
    int displayRotation_{0};
    qint64 playbackPositionMs_{0};
    qint64 playbackDurationMs_{0};
    qint64 recordElapsedMs_{0};
    int selectedMediaIndex_{-1};
    bool albumPhotos_{true};
    bool connected_{false};
    bool snapshotBusy_{false};
    bool snapshotRequestPending_{false};
    bool recordPending_{false};
    bool rtspAvailable_{false};
    bool rtspActive_{false};
    bool rtspPending_{false};
    bool uvcAvailable_{false};
    bool uvcHostConnected_{false};
    bool uvcStreaming_{false};
    QString uvcFormat_{QStringLiteral("none")};
    int uvcWidth_{0};
    int uvcHeight_{0};
    bool startMppService_{true};
    bool yoloAvailable_{false};
    bool yoloSwitchPending_{false};
    bool yoloExitScheduled_{false};
};

}  // namespace gui
}  // namespace camera
