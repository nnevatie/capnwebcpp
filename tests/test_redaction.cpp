#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>

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

struct ThrowingTarget : public RpcTarget
{
    ThrowingTarget()
    {
        method("boom", [](const json&) -> json { throw std::runtime_error("secret detail"); return json(); });
    }
};

static json parse(const std::string& s) { return json::parse(s); }

static bool testRedactionAppliedOnReject()
{
    auto target = std::make_shared<ThrowingTarget>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    // Redaction: replace message and add a stack element.
    session.setOnSendError([](const json& err) {
        json out = err;
        if (out.is_array() && out.size() >= 3 && out[0] == "error")
        {
            out[2] = "redacted"; // replace message
            if (out.size() == 3) out.push_back("STACK");
        }
        return out;
    });

    // push: call boom() which throws
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"boom"})})}).dump());
    // pull: expect reject with redacted message and stack
    json msg = parse(session.handleMessage(&data, json::array({"pull", 1}).dump()));
    bool ok = true;
    ok &= require(msg[0] == "reject" && msg[1] == 1, "redact: reject id 1");
    ok &= require(msg[2].is_array() && msg[2][0] == "error", "redact: error tuple");
    ok &= require(msg[2].size() >= 4, "redact: stack present");
    ok &= require(msg[2][2] == "redacted", "redact: message replaced");
    return ok;
}

static bool testBuildAbortHonorsRedaction()
{
    RpcSession session(nullptr);
    RpcSessionData data;
    session.setOnSendError([](const json& err){ json e = err; if (e.is_array() && e.size()>=3) e[2] = "redacted"; return e; });
    std::string s = session.buildAbort(json::array({"error","Type","msg"}));
    json msg = parse(s);
    bool ok = true;
    ok &= require(msg[0] == "abort", "abort redact: type");
    ok &= require(msg[1].is_array() && msg[1][2] == "redacted", "abort redact: message replaced");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testRedactionAppliedOnReject();
    failed += !testBuildAbortHonorsRedaction();
    if (failed == 0)
    {
        std::cout << "All redaction tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " redaction test(s) failed" << std::endl;
    return 1;
}
