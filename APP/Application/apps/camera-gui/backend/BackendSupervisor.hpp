#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

namespace camera {
namespace gui {

class BackendSupervisor final : public QObject {
    Q_OBJECT

public:
    explicit BackendSupervisor(QObject* parent = nullptr);

    void configure(QString executable, QString configPath);
    void start();
    void stop();
    bool running() const;

signals:
    void started();
    void stopped(int exitCode, QString detail);
    void diagnostic(QString text);

private:
    QProcess process_;
    QString executable_;
    QString configPath_;
};

}  // namespace gui
}  // namespace camera
