#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace capnwebcpp
{

using json = nlohmann::json;

namespace protocol
{

// Message types supported by the wire protocol.
enum class MessageType
{
    Push,
    Pull,
    Resolve,
    Reject,
    Release,
    Abort,
    Unknown
};

// Generic representation of a protocol message.
// `params` holds all elements after the type tag.
struct Message
{
    MessageType type = MessageType::Unknown;
    json params = json::array();
};

// Parse a raw JSON string (one message) into a Message.
// Returns true on success; false otherwise.
bool parse(const std::string& text, Message& out);

// Serialize a Message into a raw JSON string.
// Note: Does not transform payloads (e.g. array escaping) — only frames the message array.
std::string serialize(const Message& msg);

// Helpers for mapping types <-> strings.
const char* toString(MessageType type);
MessageType fromString(const std::string& type);

} // namespace protocol

} // namespace capnwebcpp

