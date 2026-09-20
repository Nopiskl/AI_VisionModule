#include "camera/mpp/IpcServer.hpp"

#include "camera/common/IpcMessage.hpp"
#include "camera/mpp/MppService.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace camera {
namespace mpp {
namespace {

constexpr std::size_t kMaximumQueuedBytes = 64 * 1024;
constexpr std::size_t kMaximumInputBytes = 64 * 1024;
constexpr std::size_t kMaximumClients = 8;

bool setNonBlockingCloseOnExec(int descriptor) {
    const int statusFlags = fcntl(descriptor, F_GETFL, 0);
    if (statusFlags < 0 ||
        fcntl(descriptor, F_SETFL, statusFlags | O_NONBLOCK) != 0) {
        return false;
    }
    const int descriptorFlags = fcntl(descriptor, F_GETFD, 0);
    return descriptorFlags >= 0 &&
           fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0;
}

void drainDescriptor(int descriptor) {
    unsigned char bytes[64];
    while (read(descriptor, bytes, sizeof(bytes)) > 0) {
    }
}

}  // namespace

IpcServer::~IpcServer() {
    close();
}

Status IpcServer::listenAt(const std::string& path) {
    if (listenDescriptor_ >= 0) {
        return Status::failure("ipc_already_listening", socketPath_);
    }
    if (path.empty() || path.front() != '/' ||
        path.size() >= sizeof(static_cast<sockaddr_un*>(nullptr)->sun_path)) {
        return Status::failure("ipc_path_invalid", path);
    }

    struct stat metadata {};
    if (lstat(path.c_str(), &metadata) == 0) {
        if (!S_ISSOCK(metadata.st_mode)) {
            return Status::failure("ipc_path_conflict", path);
        }
        if (unlink(path.c_str()) != 0) {
            return Status::failure("ipc_stale_socket_remove_failed",
                                   std::strerror(errno));
        }
    } else if (errno != ENOENT) {
        return Status::failure("ipc_path_check_failed", std::strerror(errno));
    }

    listenDescriptor_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenDescriptor_ < 0) {
        return Status::failure("ipc_socket_failed", std::strerror(errno));
    }
    if (!setNonBlockingCloseOnExec(listenDescriptor_)) {
        const std::string detail = std::strerror(errno);
        close();
        return Status::failure("ipc_socket_flags_failed", detail);
    }

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (bind(listenDescriptor_, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) != 0) {
        const std::string detail = std::strerror(errno);
        close();
        return Status::failure("ipc_bind_failed", detail);
    }
    socketPath_ = path;
    if (chmod(path.c_str(), 0660) != 0 || listen(listenDescriptor_, 4) != 0) {
        const std::string detail = std::strerror(errno);
        close();
        return Status::failure("ipc_listen_failed", detail);
    }
    return Status::success();
}

Status IpcServer::run(MppService& service, int stopDescriptor) {
    if (listenDescriptor_ < 0 || stopDescriptor < 0) {
        return Status::failure("ipc_not_ready", "invalid listen/stop descriptor");
    }

    service.setEventSink(
        [this](const ipc::Message& message) { broadcast(message); });
    bool stopping = false;
    auto shutdownDeadline = std::chrono::steady_clock::time_point::max();

    while (true) {
        if (stopping && (outputsDrained() ||
                         std::chrono::steady_clock::now() >= shutdownDeadline)) {
            break;
        }

        std::vector<pollfd> descriptors;
        descriptors.reserve(3 + clients_.size());
        descriptors.push_back(
            {listenDescriptor_, static_cast<short>(stopping ? 0 : POLLIN), 0});
        descriptors.push_back({service.mailbox().descriptor(), POLLIN, 0});
        descriptors.push_back({stopDescriptor, POLLIN, 0});
        for (const Client& client : clients_) {
            short events = stopping ? 0 : POLLIN;
            if (!client.output.empty()) {
                events |= POLLOUT;
            }
            descriptors.push_back({client.descriptor, events, 0});
        }

        const int timeout = stopping ? 50 : -1;
        const int pollResult = poll(descriptors.data(), descriptors.size(), timeout);
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            service.setEventSink({});
            return Status::failure("ipc_poll_failed", std::strerror(errno));
        }
        if ((descriptors[2].revents & POLLIN) != 0) {
            drainDescriptor(stopDescriptor);
            stopping = true;
            shutdownDeadline = std::chrono::steady_clock::now();
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            service.drainPipelineEvents();
        }
        if (!stopping && (descriptors[0].revents & POLLIN) != 0) {
            acceptClients();
        }

        const std::size_t polledClientCount = descriptors.size() - 3;
        for (std::size_t index = 0; index < polledClientCount; ++index) {
            Client& client = clients_[index];
            const short revents = descriptors[index + 3].revents;
            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                client.closed = true;
                continue;
            }
            if (!stopping && (revents & POLLIN) != 0) {
                readClient(client, service);
            }
            if (!client.closed && (revents & POLLOUT) != 0) {
                writeClient(client);
            }
        }
        removeClosedClients();

        if (!stopping && service.shutdownRequested()) {
            stopping = true;
            shutdownDeadline = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(500);
        }
    }

    service.setEventSink({});
    return Status::success();
}

void IpcServer::close() noexcept {
    for (Client& client : clients_) {
        if (client.descriptor >= 0) {
            ::close(client.descriptor);
        }
    }
    clients_.clear();
    if (listenDescriptor_ >= 0) {
        ::close(listenDescriptor_);
        listenDescriptor_ = -1;
    }
    if (!socketPath_.empty()) {
        unlink(socketPath_.c_str());
        socketPath_.clear();
    }
}

void IpcServer::acceptClients() {
    for (;;) {
        const int descriptor = accept(listenDescriptor_, nullptr, nullptr);
        if (descriptor < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (!setNonBlockingCloseOnExec(descriptor)) {
            ::close(descriptor);
            continue;
        }
        if (clients_.size() >= kMaximumClients) {
            ::close(descriptor);
            continue;
        }
        clients_.push_back({descriptor, {}, {}, false});
    }
}

void IpcServer::readClient(Client& client, MppService& service) {
    char buffer[2048];
    for (;;) {
        const ssize_t received = recv(client.descriptor, buffer, sizeof(buffer), 0);
        if (received == 0) {
            client.closed = true;
            return;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                client.closed = true;
            }
            break;
        }
        client.input.append(buffer, static_cast<std::size_t>(received));
        if (client.input.size() > kMaximumInputBytes ||
            (client.input.size() > ipc::kMaximumLineBytes &&
             client.input.find('\n') == std::string::npos)) {
            client.closed = true;
            return;
        }
    }

    for (;;) {
        const std::size_t newline = client.input.find('\n');
        if (newline == std::string::npos) {
            break;
        }
        const std::string line = client.input.substr(0, newline + 1);
        client.input.erase(0, newline + 1);
        const ipc::ParseResult parsed = ipc::parseLine(line);
        if (!parsed.ok) {
            ipc::Message event = ipc::makeEvent("protocol_error");
            event.fields["code"] = "message_invalid";
            event.fields["detail"] = parsed.error;
            queue(client, event);
            continue;
        }
        if (parsed.message.type != ipc::MessageType::Request) {
            ipc::Message event = ipc::makeEvent("protocol_error");
            event.fields["code"] = "request_required";
            queue(client, event);
            continue;
        }
        dispatching_ = true;
        ipc::Message response;
        try {
            response = service.handle(parsed.message);
        } catch (const std::exception& error) {
            response = ipc::makeResponse(parsed.message, false, "internal_error",
                                         error.what());
        } catch (...) {
            response = ipc::makeResponse(parsed.message, false, "internal_error",
                                         "unknown service exception");
        }
        dispatching_ = false;
        queue(client, response);
        for (const ipc::Message& event : deferredEvents_) {
            broadcast(event);
        }
        deferredEvents_.clear();
    }
}

void IpcServer::writeClient(Client& client) {
    while (!client.output.empty()) {
        const ssize_t written = send(client.descriptor, client.output.data(),
                                     client.output.size(), MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                client.closed = true;
            }
            return;
        }
        if (written == 0) {
            client.closed = true;
            return;
        }
        client.output.erase(0, static_cast<std::size_t>(written));
    }
}

void IpcServer::queue(Client& client, const ipc::Message& message) {
    try {
        const std::string serialized = ipc::serializeLine(message);
        if (client.output.size() + serialized.size() > kMaximumQueuedBytes) {
            client.closed = true;
            return;
        }
        client.output += serialized;
    } catch (const std::exception&) {
        client.closed = true;
    }
}

void IpcServer::broadcast(const ipc::Message& message) {
    if (dispatching_) {
        try {
            deferredEvents_.push_back(message);
        } catch (const std::exception&) {
        }
        return;
    }
    for (Client& client : clients_) {
        if (!client.closed) {
            queue(client, message);
        }
    }
}

void IpcServer::removeClosedClients() noexcept {
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(), [](Client& client) {
            if (!client.closed) {
                return false;
            }
            if (client.descriptor >= 0) {
                ::close(client.descriptor);
                client.descriptor = -1;
            }
            return true;
        }),
        clients_.end());
}

bool IpcServer::outputsDrained() const {
    return std::all_of(clients_.begin(), clients_.end(),
                       [](const Client& client) { return client.output.empty(); });
}

}  // namespace mpp
}  // namespace camera
