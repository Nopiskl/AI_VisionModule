#include "backend/IpcClient.hpp"

#include "camera/common/IpcMessage.hpp"

#include <QFile>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <exception>
#include <utility>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace camera {
namespace gui {
namespace {

constexpr int kMaximumBufferedBytes = 64 * 1024;

QVariantMap toVariantMap(const std::map<std::string, std::string>& fields) {
    QVariantMap result;
    for (const auto& field : fields) {
        result.insert(QString::fromStdString(field.first),
                      QString::fromStdString(field.second));
    }
    return result;
}

bool prepareDescriptor(int descriptor) {
    const int statusFlags = ::fcntl(descriptor, F_GETFL, 0);
    if (statusFlags < 0 ||
        ::fcntl(descriptor, F_SETFL, statusFlags | O_NONBLOCK) < 0) {
        return false;
    }
    const int descriptorFlags = ::fcntl(descriptor, F_GETFD, 0);
    return descriptorFlags >= 0 &&
           ::fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0;
}

QString systemError(const QString& prefix, int errorNumber) {
    return QStringLiteral("%1: %2")
        .arg(prefix, QString::fromLocal8Bit(std::strerror(errorNumber)));
}

}  // namespace

IpcClient::IpcClient(QObject* parent) : QObject(parent) {
    reconnectTimer_.setInterval(500);
    reconnectTimer_.setSingleShot(true);
    connect(&reconnectTimer_, &QTimer::timeout, this, &IpcClient::connectNow);
}

IpcClient::~IpcClient() {
    stop();
}

void IpcClient::setSocketPath(QString path) {
    socketPath_ = std::move(path);
}

void IpcClient::start() {
    if (enabled_) {
        return;
    }
    enabled_ = true;
    connectNow();
}

void IpcClient::stop() {
    enabled_ = false;
    reconnectTimer_.stop();
    disconnectSocket();
}

quint64 IpcClient::sendRequest(const QString& name, const QVariantMap& fields) {
    if (!connected()) {
        emit diagnostic(tr("MPP IPC 尚未连接"));
        return 0;
    }

    const quint64 requestId = nextRequestId_++;
    if (nextRequestId_ == 0) {
        nextRequestId_ = 1;
    }
    ipc::Message request = ipc::makeRequest(requestId, name.toStdString());
    for (auto iterator = fields.constBegin(); iterator != fields.constEnd();
         ++iterator) {
        request.fields[iterator.key().toStdString()] =
            iterator.value().toString().toStdString();
    }

    try {
        const std::string line = ipc::serializeLine(request);
        if (line.size() > static_cast<std::size_t>(kMaximumBufferedBytes) ||
            output_.size() > kMaximumBufferedBytes - static_cast<int>(line.size())) {
            emit diagnostic(tr("MPP IPC 输出队列超过上限，重新连接"));
            disconnectSocket();
            return 0;
        }
        output_.append(line.data(), static_cast<int>(line.size()));
        flushOutput();
        return connected() ? requestId : 0;
    } catch (const std::exception& error) {
        emit diagnostic(QString::fromLocal8Bit(error.what()));
        return 0;
    }
}

bool IpcClient::connected() const {
    return connected_;
}

void IpcClient::connectNow() {
    if (!enabled_ || descriptor_ >= 0 || socketPath_.isEmpty()) {
        return;
    }

    const QByteArray encodedPath = QFile::encodeName(socketPath_);
    sockaddr_un address{};
    if (encodedPath.isEmpty() ||
        encodedPath.size() >= static_cast<int>(sizeof(address.sun_path))) {
        emit diagnostic(tr("MPP IPC socket 路径无效或过长：%1").arg(socketPath_));
        return;
    }

    descriptor_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor_ < 0) {
        const int errorNumber = errno;
        emit diagnostic(systemError(tr("创建 MPP IPC socket 失败"), errorNumber));
        scheduleReconnect();
        return;
    }
    if (!prepareDescriptor(descriptor_)) {
        const int errorNumber = errno;
        emit diagnostic(systemError(tr("设置 MPP IPC socket 失败"), errorNumber));
        disconnectSocket();
        return;
    }

    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, encodedPath.constData(),
                static_cast<std::size_t>(encodedPath.size() + 1));
    if (::connect(descriptor_, reinterpret_cast<const sockaddr*>(&address),
                  sizeof(address)) == 0) {
        connectedSuccessfully();
        return;
    }

    const int errorNumber = errno;
    if (errorNumber != EINPROGRESS) {
        if (errorNumber != ENOENT && errorNumber != ECONNREFUSED) {
            emit diagnostic(systemError(tr("连接 MPP IPC 失败"), errorNumber));
        }
        disconnectSocket();
        return;
    }

    connecting_ = true;
    writeNotifier_ = new QSocketNotifier(descriptor_, QSocketNotifier::Write, this);
    connect(writeNotifier_, &QSocketNotifier::activated, this,
            [this](int) { writeAvailable(); });
}

void IpcClient::connectedSuccessfully() {
    connecting_ = false;
    connected_ = true;
    reconnectTimer_.stop();

    if (writeNotifier_ != nullptr) {
        writeNotifier_->setEnabled(false);
        writeNotifier_->deleteLater();
        writeNotifier_ = nullptr;
    }
    readNotifier_ = new QSocketNotifier(descriptor_, QSocketNotifier::Read, this);
    connect(readNotifier_, &QSocketNotifier::activated, this,
            [this](int) { readAvailable(); });
    emit connectedChanged(true);
}

void IpcClient::disconnectSocket() {
    const bool wasConnected = connected_;
    connected_ = false;
    connecting_ = false;

    if (readNotifier_ != nullptr) {
        readNotifier_->setEnabled(false);
        readNotifier_->deleteLater();
        readNotifier_ = nullptr;
    }
    if (writeNotifier_ != nullptr) {
        writeNotifier_->setEnabled(false);
        writeNotifier_->deleteLater();
        writeNotifier_ = nullptr;
    }
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
    input_.clear();
    output_.clear();

    if (wasConnected) {
        emit connectedChanged(false);
    }
    scheduleReconnect();
}

void IpcClient::scheduleReconnect() {
    if (enabled_ && descriptor_ < 0 && !reconnectTimer_.isActive()) {
        reconnectTimer_.start();
    }
}

void IpcClient::readAvailable() {
    if (!connected_ || descriptor_ < 0) {
        return;
    }

    char buffer[4096];
    for (;;) {
        const ssize_t count = ::recv(descriptor_, buffer, sizeof(buffer), 0);
        if (count > 0) {
            if (input_.size() >
                kMaximumBufferedBytes - static_cast<int>(count)) {
                emit diagnostic(tr("MPP IPC 输入超过上限，重新连接"));
                disconnectSocket();
                return;
            }
            input_.append(buffer, static_cast<int>(count));
            continue;
        }
        if (count == 0) {
            disconnectSocket();
            return;
        }
        const int errorNumber = errno;
        if (errorNumber == EINTR) {
            continue;
        }
        if (errorNumber != EAGAIN && errorNumber != EWOULDBLOCK) {
            emit diagnostic(systemError(tr("读取 MPP IPC 失败"), errorNumber));
            disconnectSocket();
            return;
        }
        break;
    }

    for (;;) {
        const int newline = input_.indexOf('\n');
        if (newline < 0) {
            break;
        }
        const QByteArray line = input_.left(newline + 1);
        input_.remove(0, newline + 1);
        processLine(line);
    }
}

void IpcClient::writeAvailable() {
    if (descriptor_ < 0) {
        return;
    }
    if (connecting_) {
        int socketError = 0;
        socklen_t length = sizeof(socketError);
        if (::getsockopt(descriptor_, SOL_SOCKET, SO_ERROR, &socketError, &length) < 0) {
            socketError = errno;
        }
        if (socketError != 0) {
            if (socketError != ENOENT && socketError != ECONNREFUSED) {
                emit diagnostic(systemError(tr("连接 MPP IPC 失败"), socketError));
            }
            disconnectSocket();
            return;
        }
        connectedSuccessfully();
    }
    flushOutput();
}

void IpcClient::flushOutput() {
    if (!connected_ || descriptor_ < 0) {
        return;
    }
    while (!output_.isEmpty()) {
        const ssize_t count = ::send(descriptor_, output_.constData(),
                                     static_cast<std::size_t>(output_.size()),
                                     MSG_NOSIGNAL);
        if (count > 0) {
            output_.remove(0, static_cast<int>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (writeNotifier_ == nullptr) {
                writeNotifier_ =
                    new QSocketNotifier(descriptor_, QSocketNotifier::Write, this);
                connect(writeNotifier_, &QSocketNotifier::activated, this,
                        [this](int) { writeAvailable(); });
            }
            return;
        }
        const int errorNumber = count < 0 ? errno : EPIPE;
        emit diagnostic(systemError(tr("写入 MPP IPC 失败"), errorNumber));
        disconnectSocket();
        return;
    }

    if (writeNotifier_ != nullptr && !connecting_) {
        writeNotifier_->setEnabled(false);
        writeNotifier_->deleteLater();
        writeNotifier_ = nullptr;
    }
}

void IpcClient::processLine(const QByteArray& line) {
    const ipc::ParseResult parsed = ipc::parseLine(line.toStdString());
    if (!parsed.ok) {
        emit diagnostic(tr("无效的 MPP IPC 消息：%1")
                            .arg(QString::fromStdString(parsed.error)));
        return;
    }
    const QVariantMap fields = toVariantMap(parsed.message.fields);
    if (parsed.message.type == ipc::MessageType::Event) {
        emit eventReceived(QString::fromStdString(parsed.message.name), fields);
        return;
    }
    if (parsed.message.type != ipc::MessageType::Response) {
        emit diagnostic(tr("MPP IPC 返回了非 response 消息"));
        return;
    }
    emit responseReceived(
        parsed.message.requestId, QString::fromStdString(parsed.message.name),
        parsed.message.field("ok") == "1",
        QString::fromStdString(parsed.message.field("code")),
        QString::fromStdString(parsed.message.field("detail")), fields);
}

}  // namespace gui
}  // namespace camera
