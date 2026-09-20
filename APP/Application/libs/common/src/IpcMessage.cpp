#include "camera/common/IpcMessage.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace camera {
namespace ipc {
namespace {

bool isSafeByte(unsigned char value) {
    return std::isalnum(value) != 0 || value == '-' || value == '_' ||
           value == '.' || value == '/' || value == ':';
}

char hexDigit(unsigned int value) {
    return static_cast<char>(value < 10 ? ('0' + value) : ('A' + value - 10));
}

int fromHex(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

std::string encode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char byte : value) {
        if (isSafeByte(byte)) {
            result.push_back(static_cast<char>(byte));
        } else {
            result.push_back('%');
            result.push_back(hexDigit((byte >> 4U) & 0x0fU));
            result.push_back(hexDigit(byte & 0x0fU));
        }
    }
    return result;
}

bool decode(const std::string& value, std::string* result) {
    result->clear();
    result->reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%') {
            result->push_back(value[index]);
            continue;
        }
        if (index + 2 >= value.size()) {
            return false;
        }
        const int high = fromHex(value[index + 1]);
        const int low = fromHex(value[index + 2]);
        if (high < 0 || low < 0) {
            return false;
        }
        result->push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
    return true;
}

bool parseUnsigned(const std::string& text, std::uint64_t* value) {
    if (text.empty() ||
        !std::all_of(text.begin(), text.end(),
                     [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        *value = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool validKey(const std::string& key) {
    if (key.empty()) {
        return false;
    }
    return std::all_of(key.begin(), key.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    });
}

bool parseType(const std::string& value, MessageType* type) {
    if (value == "request") {
        *type = MessageType::Request;
        return true;
    }
    if (value == "response") {
        *type = MessageType::Response;
        return true;
    }
    if (value == "event") {
        *type = MessageType::Event;
        return true;
    }
    return false;
}

}  // namespace

std::string Message::field(const std::string& key,
                           const std::string& fallback) const {
    const auto found = fields.find(key);
    return found == fields.end() ? fallback : found->second;
}

bool Message::hasField(const std::string& key) const {
    return fields.find(key) != fields.end();
}

const char* toString(MessageType type) {
    switch (type) {
        case MessageType::Request:
            return "request";
        case MessageType::Response:
            return "response";
        case MessageType::Event:
            return "event";
    }
    return "unknown";
}

ParseResult parseLine(const std::string& input) {
    ParseResult result;
    if (input.empty()) {
        result.error = "empty message";
        return result;
    }
    if (input.size() > kMaximumLineBytes) {
        result.error = "message exceeds protocol limit";
        return result;
    }

    std::string line = input;
    if (!line.empty() && line.back() == '\n') {
        line.pop_back();
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    std::map<std::string, std::string> rawFields;
    std::size_t offset = 0;
    while (offset <= line.size()) {
        const std::size_t end = line.find('\t', offset);
        const std::string token = line.substr(
            offset, end == std::string::npos ? std::string::npos : end - offset);
        const std::size_t equals = token.find('=');
        if (equals == std::string::npos || equals == 0) {
            result.error = "malformed field";
            return result;
        }
        const std::string key = token.substr(0, equals);
        if (!validKey(key) || rawFields.find(key) != rawFields.end()) {
            result.error = "invalid or duplicate field: " + key;
            return result;
        }
        std::string decoded;
        if (!decode(token.substr(equals + 1), &decoded)) {
            result.error = "invalid percent encoding in field: " + key;
            return result;
        }
        rawFields.emplace(key, std::move(decoded));
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1;
    }

    const auto version = rawFields.find("v");
    const auto type = rawFields.find("type");
    const auto name = rawFields.find("name");
    if (version == rawFields.end() || type == rawFields.end() ||
        name == rawFields.end() || name->second.empty()) {
        result.error = "missing v, type or name";
        return result;
    }

    std::uint64_t parsedVersion = 0;
    if (!parseUnsigned(version->second, &parsedVersion) ||
        parsedVersion > std::numeric_limits<std::uint32_t>::max()) {
        result.error = "invalid protocol version";
        return result;
    }
    result.message.version = static_cast<std::uint32_t>(parsedVersion);
    if (!parseType(type->second, &result.message.type)) {
        result.error = "invalid message type";
        return result;
    }
    result.message.name = name->second;

    const auto requestId = rawFields.find("request_id");
    if (result.message.type != MessageType::Event) {
        if (requestId == rawFields.end() ||
            !parseUnsigned(requestId->second, &result.message.requestId) ||
            result.message.requestId == 0) {
            result.error = "request_id must be a non-zero integer";
            return result;
        }
    } else if (requestId != rawFields.end() &&
               !parseUnsigned(requestId->second, &result.message.requestId)) {
        result.error = "invalid event request_id";
        return result;
    }

    rawFields.erase("v");
    rawFields.erase("type");
    rawFields.erase("request_id");
    rawFields.erase("name");
    result.message.fields = std::move(rawFields);
    result.ok = true;
    return result;
}

std::string serializeLine(const Message& message) {
    if (message.name.empty()) {
        throw std::invalid_argument("IPC message name must not be empty");
    }
    if (message.type != MessageType::Event && message.requestId == 0) {
        throw std::invalid_argument("IPC request_id must be non-zero");
    }
    std::ostringstream stream;
    stream << "v=" << message.version << "\ttype=" << toString(message.type);
    if (message.type != MessageType::Event || message.requestId != 0) {
        stream << "\trequest_id=" << message.requestId;
    }
    stream << "\tname=" << encode(message.name);
    for (const auto& entry : message.fields) {
        if (!validKey(entry.first) || entry.first == "v" ||
            entry.first == "type" || entry.first == "request_id" ||
            entry.first == "name") {
            throw std::invalid_argument("invalid IPC field key: " + entry.first);
        }
        stream << '\t' << entry.first << '=' << encode(entry.second);
    }
    stream << '\n';
    const auto serialized = stream.str();
    if (serialized.size() > kMaximumLineBytes) {
        throw std::length_error("serialized IPC message exceeds protocol limit");
    }
    return serialized;
}

Message makeRequest(std::uint64_t requestId, const std::string& name) {
    Message message;
    message.type = MessageType::Request;
    message.requestId = requestId;
    message.name = name;
    return message;
}

Message makeResponse(const Message& request, bool ok,
                     const std::string& code, const std::string& detail) {
    Message message;
    message.type = MessageType::Response;
    message.requestId = request.requestId;
    message.name = request.name;
    message.fields["ok"] = ok ? "1" : "0";
    if (!code.empty()) {
        message.fields["code"] = code;
    }
    if (!detail.empty()) {
        message.fields["detail"] = detail;
    }
    return message;
}

Message makeEvent(const std::string& name) {
    Message message;
    message.type = MessageType::Event;
    message.name = name;
    return message;
}

}  // namespace ipc
}  // namespace camera
