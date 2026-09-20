#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class QSocketNotifier;

namespace camera {
namespace gui {

class IpcClient final : public QObject {
    Q_OBJECT

public:
    explicit IpcClient(QObject* parent = nullptr);
    ~IpcClient() override;

    void setSocketPath(QString path);
    void start();
    void stop();
    quint64 sendRequest(const QString& name,
                        const QVariantMap& fields = QVariantMap{});
    bool connected() const;

signals:
    void connectedChanged(bool connected);
    void responseReceived(quint64 requestId, QString name, bool ok,
                          QString code, QString detail, QVariantMap fields);
    void eventReceived(QString name, QVariantMap fields);
    void diagnostic(QString text);

private slots:
    void connectNow();
    void readAvailable();
    void writeAvailable();

private:
    void connectedSuccessfully();
    void disconnectSocket();
    void scheduleReconnect();
    void flushOutput();
    void processLine(const QByteArray& line);

    QTimer reconnectTimer_;
    QString socketPath_;
    QByteArray input_;
    QByteArray output_;
    QSocketNotifier* readNotifier_{nullptr};
    QSocketNotifier* writeNotifier_{nullptr};
    int descriptor_{-1};
    quint64 nextRequestId_{1};
    bool enabled_{false};
    bool connecting_{false};
    bool connected_{false};
};

}  // namespace gui
}  // namespace camera
