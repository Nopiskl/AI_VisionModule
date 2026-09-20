#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace camera {
namespace ipc {

constexpr std::uint32_t kProtocolVersion = 1;
constexpr std::size_t kMaximumLineBytes = 8192;

enum class MessageType {
    Request,
    Response,
    Event,
};

struct Message {
    std::uint32_t version{kProtocolVersion};
    MessageType type{MessageType::Request};
    std::uint64_t requestId{0};
    std::string name;
    std::map<std::string, std::string> fields;

    std::string field(const std::string& key,
                      const std::string& fallback = {}) const;
    bool hasField(const std::string& key) const;
};

struct ParseResult {
    bool ok{false};
    Message message;
    std::string error;
};

ParseResult parseLine(const std::string& line);
std::string serializeLine(const Message& message);

Message makeRequest(std::uint64_t requestId, const std::string& name);
Message makeResponse(const Message& request, bool ok,
                     const std::string& code = {},
                     const std::string& detail = {});
Message makeEvent(const std::string& name);

const char* toString(MessageType type);

}  // namespace ipc
}  // namespace camera
