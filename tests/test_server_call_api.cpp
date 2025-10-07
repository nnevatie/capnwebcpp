#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/transports/accum_transport.h>

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

static json parse(const std::string& s) { return json::parse(s); }

static bool testCallClientMethod()
{
    std::vector<std::string> outbox;
    auto transport = std::make_shared<AccumTransport>(outbox);
    RpcSession session(nullptr);
    RpcSessionData data; data.transport = transport;

    int promiseId = session.callClientMethod(&data, /*exportId=*/9, /*method=*/"greet", json::array({"Bob"}));
    bool ok = true;
    ok &= require(promiseId < 0, "api: promise id negative");
    ok &= require(outbox.size() == 2, "api: push+pull sent");
    auto push = parse(outbox[0]);
    auto pull = parse(outbox[1]);
    ok &= require(push[0] == "push" && push[1][0] == "pipeline" && push[1][1] == 9, "api: pipeline to export 9");
    ok &= require(pull[0] == "pull" && pull[1] == 1, "api: pull import 1");

    // Simulate client resolve; expect forwarded resolve to promise id.
    session.handleMessage(&data, json::array({"resolve", 1, "Hello, Bob!"}).dump());
    ok &= require(outbox.size() == 3, "api: forwarded resolve");
    auto fwd = parse(outbox[2]);
    ok &= require(fwd[0] == "resolve" && fwd[1] == promiseId && fwd[2] == "Hello, Bob!", "api: forwarded matches");
    return ok;
}

static bool testCallClientGet()
{
    std::vector<std::string> outbox;
    auto transport = std::make_shared<AccumTransport>(outbox);
    RpcSession session(nullptr);
    RpcSessionData data; data.transport = transport;

    int promiseId = session.callClient(&data, /*exportId=*/11, json::array({"version"}));
    bool ok = true;
    ok &= require(promiseId < 0, "api get: promise id negative");
    ok &= require(outbox.size() == 2, "api get: push+pull sent");
    auto push = parse(outbox[0]);
    ok &= require(push[1].size() == 3, "api get: pipeline without args");

    // Simulate client resolve; expect forwarded resolve to promise id.
    session.handleMessage(&data, json::array({"resolve", 1, json::array({"version","1.0.0"})}).dump());
    ok &= require(outbox.size() == 3, "api get: forwarded resolve");
    auto fwd = parse(outbox[2]);
    ok &= require(fwd[0] == "resolve" && fwd[1] == promiseId, "api get: forwarded id matches");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testCallClientMethod();
    failed += !testCallClientGet();
    if (failed == 0)
    {
        std::cout << "All server-call API tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " server-call API test(s) failed" << std::endl;
    return 1;
}

