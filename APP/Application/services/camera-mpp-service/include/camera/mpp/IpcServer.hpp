#pragma once

#include "camera/common/IpcMessage.hpp"
#include "camera/mpp/Status.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace camera {
namespace mpp {

class MppService;

class IpcServer {
public:
    IpcServer() = default;
    ~IpcServer();

    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    Status listenAt(const std::string& path);
    Status run(MppService& service, int stopDescriptor);
    void close() noexcept;

private:
    struct Client {
        int descriptor{-1};
        std::string input;
        std::string output;
        bool closed{false};
    };

    void acceptClients();
    void readClient(Client& client, MppService& service);
    void writeClient(Client& client);
    void queue(Client& client, const ipc::Message& message);
    void broadcast(const ipc::Message& message);
    void removeClosedClients() noexcept;
    bool outputsDrained() const;

    int listenDescriptor_{-1};
    std::string socketPath_;
    std::vector<Client> clients_;
    std::vector<ipc::Message> deferredEvents_;
    bool dispatching_{false};
};

}  // namespace mpp
}  // namespace camera
