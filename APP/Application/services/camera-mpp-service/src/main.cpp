#include "camera/mpp/IpcServer.hpp"
#include "camera/mpp/MppService.hpp"
#include "camera/mpp/ServiceConfig.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/prctl.h>
#include <unistd.h>

namespace {

volatile sig_atomic_t gStopWriteDescriptor = -1;

void requestStop(int) {
    const int descriptor = static_cast<int>(gStopWriteDescriptor);
    if (descriptor >= 0) {
        const unsigned char marker = 1;
        const int savedError = errno;
        const ssize_t ignored = write(descriptor, &marker, sizeof(marker));
        (void)ignored;
        errno = savedError;
    }
}

bool prepareDescriptor(int descriptor) {
    const int statusFlags = fcntl(descriptor, F_GETFL, 0);
    const int descriptorFlags = fcntl(descriptor, F_GETFD, 0);
    return statusFlags >= 0 && descriptorFlags >= 0 &&
           fcntl(descriptor, F_SETFL, statusFlags | O_NONBLOCK) == 0 &&
           fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0;
}

void printUsage(const char* program) {
    std::cerr << "Usage: " << program
              << " [--config /etc/v851s-camera/mpp-service.conf]"
                 " [--exit-with-parent]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = "/etc/v851s-camera/mpp-service.conf";
    bool exitWithParent = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config" && index + 1 < argc) {
            configPath = argv[++index];
        } else if (argument == "--exit-with-parent") {
            exitWithParent = true;
        } else if (argument == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            printUsage(argv[0]);
            return 2;
        }
    }

    camera::mpp::ServiceConfig config;
    camera::mpp::Status status =
        camera::mpp::ServiceConfig::load(configPath, &config);
    if (!status.ok) {
        std::cerr << status.code << ": " << status.detail << '\n';
        return 1;
    }

    int stopDescriptors[2] = {-1, -1};
    if (pipe(stopDescriptors) != 0 || !prepareDescriptor(stopDescriptors[0]) ||
        !prepareDescriptor(stopDescriptors[1])) {
        std::cerr << "signal_pipe_failed: " << std::strerror(errno) << '\n';
        if (stopDescriptors[0] >= 0) {
            close(stopDescriptors[0]);
        }
        if (stopDescriptors[1] >= 0) {
            close(stopDescriptors[1]);
        }
        return 1;
    }
    gStopWriteDescriptor = stopDescriptors[1];
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    std::signal(SIGPIPE, SIG_IGN);
    if (exitWithParent) {
        const pid_t expectedParent = getppid();
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0) {
            std::cerr << "parent_death_signal_failed: " << std::strerror(errno)
                      << '\n';
            gStopWriteDescriptor = -1;
            close(stopDescriptors[0]);
            close(stopDescriptors[1]);
            return 1;
        }
        if (getppid() != expectedParent) {
            requestStop(SIGTERM);
        }
    }

    try {
        camera::mpp::MppService service(config);
        status = service.initialize();
        if (status.ok) {
            camera::mpp::IpcServer server;
            status = server.listenAt(config.socketPath);
            if (status.ok) {
                status = server.run(service, stopDescriptors[0]);
            }
        }

        const camera::mpp::Status shutdownStatus = service.shutdown();
        if (status.ok && !shutdownStatus.ok) {
            status = shutdownStatus;
        }
    } catch (const std::exception& error) {
        status = camera::mpp::Status::failure("service_exception", error.what());
    } catch (...) {
        status = camera::mpp::Status::failure(
            "service_exception", "unknown service exception");
    }
    gStopWriteDescriptor = -1;
    close(stopDescriptors[0]);
    close(stopDescriptors[1]);

    if (!status.ok) {
        std::cerr << status.code << ": " << status.detail << '\n';
        return 1;
    }
    return 0;
}
