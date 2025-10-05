#include "capnwebcpp/protocol.h"

namespace capnwebcpp
{
namespace protocol
{

static constexpr const char* PUSH = "push";
static constexpr const char* PULL = "pull";
static constexpr const char* RESOLVE = "resolve";
static constexpr const char* REJECT = "reject";
static constexpr const char* RELEASE = "release";
static constexpr const char* ABORT = "abort";

const char* toString(MessageType type)
{
    switch (type)
    {
        case MessageType::Push: return PUSH;
        case MessageType::Pull: return PULL;
        case MessageType::Resolve: return RESOLVE;
        case MessageType::Reject: return REJECT;
        case MessageType::Release: return RELEASE;
        case MessageType::Abort: return ABORT;
        default: return "unknown";
    }
}

MessageType fromString(const std::string& type)
{
    if (type == PUSH) return MessageType::Push;
    if (type == PULL) return MessageType::Pull;
    if (type == RESOLVE) return MessageType::Resolve;
    if (type == REJECT) return MessageType::Reject;
    if (type == RELEASE) return MessageType::Release;
    if (type == ABORT) return MessageType::Abort;
    return MessageType::Unknown;
}

bool parse(const std::string& text, Message& out)
{
    try
    {
        json msg = json::parse(text);
        if (!msg.is_array() || msg.empty() || !msg[0].is_string())
            return false;

        out.type = fromString(msg[0]);
        out.params = json::array();
        for (size_t i = 1; i < msg.size(); ++i)
            out.params.push_back(msg[i]);
        return true;
    }
    catch (...) {
        return false;
    }
}

std::string serialize(const Message& msg)
{
    json arr = json::array();
    arr.push_back(toString(msg.type));
    if (msg.params.is_array())
    {
        for (const auto& p : msg.params)
            arr.push_back(p);
    }
    else if (!msg.params.is_null())
    {
        // Backwards-compatible: treat non-array params as a single parameter.
        arr.push_back(msg.params);
    }
    return arr.dump();
}

} // namespace protocol
} // namespace capnwebcpp

