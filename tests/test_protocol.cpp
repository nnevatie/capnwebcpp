#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/protocol.h>
#include <capnwebcpp/serialize.h>

using json = nlohmann::json;

static bool require(bool cond, const std::string& msg)
{
    if (!cond)
    {
        std::cerr << "TEST FAILED: " << msg << std::endl;
        return false;
    }
    return true;
}

static bool testProtocolParseSerialize()
{
    using namespace capnwebcpp::protocol;
    bool ok = true;

    // Round-trip: resolve
    {
        Message msg;
        bool parsed = parse("[\"resolve\", 42, \"ok\"]", msg);
        ok &= require(parsed, "protocol parse: resolve parsed");
        ok &= require(msg.type == MessageType::Resolve, "protocol parse: type resolve");
        ok &= require(msg.params.is_array() && msg.params.size() == 2, "protocol parse: resolve params size");
        ok &= require(msg.params[0] == 42, "protocol parse: resolve id");
        ok &= require(msg.params[1] == "ok", "protocol parse: resolve payload");

        std::string s = serialize(msg);
        ok &= require(json::parse(s) == json::parse("[\"resolve\",42,\"ok\"]"), "protocol serialize: resolve shape");
    }

    // Round-trip: reject
    {
        Message msg;
        bool parsed = parse("[\"reject\", 3, [\"error\", \"Name\", \"msg\"]]", msg);
        ok &= require(parsed, "protocol parse: reject parsed");
        ok &= require(msg.type == MessageType::Reject, "protocol parse: type reject");

        std::string s = serialize(msg);
        ok &= require(json::parse(s) == json::parse("[\"reject\",3,[\"error\",\"Name\",\"msg\"]]"), "protocol serialize: reject shape");
    }

    // Parse: push and pull
    {
        Message msg;
        ok &= require(parse("[\"push\", [\"pipeline\", 1, [\"foo\"]]]", msg), "protocol parse: push parsed");
        ok &= require(msg.type == MessageType::Push, "protocol parse: push type");
        ok &= require(parse("[\"pull\", 5]", msg), "protocol parse: pull parsed");
        ok &= require(msg.type == MessageType::Pull, "protocol parse: pull type");
    }

    // Parse failure cases
    {
        Message msg;
        ok &= require(!parse("{}", msg), "protocol parse: reject object");
        ok &= require(!parse("[]", msg), "protocol parse: reject empty array");
        ok &= require(!parse("[123]", msg), "protocol parse: reject non-string type tag");
    }

    return ok;
}

static bool testSerializeHelpers()
{
    using namespace capnwebcpp::serialize;
    bool ok = true;

    // wrapArrayIfNeeded
    ok &= require(wrapArrayIfNeeded(json::array({1, 2})) == json::array({ json::array({1, 2}) }), "wrapArrayIfNeeded: array is wrapped");
    ok &= require(wrapArrayIfNeeded(json(5)) == json(5), "wrapArrayIfNeeded: primitive unchanged");
    ok &= require(wrapArrayIfNeeded(json{{"a", 1}}) == json{{"a", 1}}, "wrapArrayIfNeeded: object unchanged");

    // makeError
    ok &= require(makeError("TypeError", "bad") == json::array({"error", "TypeError", "bad"}), "makeError: shape");

    return ok;
}

int main()
{
    int failed = 0;
    failed += !testProtocolParseSerialize();
    failed += !testSerializeHelpers();
    if (failed == 0)
    {
        std::cout << "All protocol/serialize tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " protocol/serialize test(s) failed" << std::endl;
    return 1;
}

