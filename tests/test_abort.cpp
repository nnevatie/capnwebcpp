#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/serialize.h>

using json = nlohmann::json;
using namespace capnwebcpp;

static bool require(bool cond, const std::string& msg)
{
    if (!cond)
    {
        std::cerr << "TEST FAILED: " << msg << std::endl;
        return false;
    }
    return true;
}

static json parse(const std::string& s)
{
    return json::parse(s);
}

static bool testInboundAbortTriggersCallbacks()
{
    RpcSession session(nullptr);
    RpcSessionData data;

    bool called = false;
    std::string gotReason;
    session.registerOnBroken([&](const std::string& reason)
    {
        called = true;
        gotReason = reason;
    });

    // Send an abort frame with an error tuple payload.
    std::string abortMsg = json::array({ "abort", json::array({ "error", "Type", "msg" }) }).dump();
    std::string resp = session.handleMessage(&data, abortMsg);

    bool ok = true;
    ok &= require(resp.empty(), "abort: no response");
    ok &= require(session.isAborted(), "abort: session marked aborted");
    // Tables are cleaned up on abort.
    ok &= require(data.exports.empty() && data.imports.empty(), "abort: tables cleared");
    ok &= require(called, "abort: onBroken callback called");
    try
    {
        json reason = json::parse(gotReason);
        ok &= require(reason.is_array() && reason.size() >= 3 && reason[0] == "error", "abort: reason is error tuple JSON");
        ok &= require(reason[1] == "Type" && reason[2] == "msg", "abort: reason contents");
    }
    catch (...)
    {
        ok &= require(false, "abort: reason not valid JSON");
    }
    return ok;
}

static bool testBuildAbortFrame()
{
    RpcSession session(nullptr);
    json err = serialize::makeError("ServerError", "oops");
    std::string frame = session.buildAbort(err);
    json msg = parse(frame);

    bool ok = true;
    ok &= require(msg.is_array() && msg.size() == 2, "buildAbort: shape [type, payload]");
    ok &= require(msg[0] == "abort", "buildAbort: type abort");
    ok &= require(msg[1].is_array() && msg[1].size() >= 3 && msg[1][0] == "error", "buildAbort: payload error tuple");
    ok &= require(msg[1][1] == "ServerError" && msg[1][2] == "oops", "buildAbort: payload contents");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testInboundAbortTriggersCallbacks();
    failed += !testBuildAbortFrame();

    if (failed == 0)
    {
        std::cout << "All abort tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " abort test(s) failed" << std::endl;
    return 1;
}
