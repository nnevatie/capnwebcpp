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

static json parse(const std::string& s)
{
    return json::parse(s);
}

// Simulate a remap that captures an export from the client and invokes a method on it.
// Verify that the server sends push+pull to the client, returns a promise in the top-level
// result, and forwards the client's resolve to that promise export ID.
static bool testClientCallPathViaRemap()
{
    std::vector<std::string> outbox;
    auto transport = std::make_shared<AccumTransport>(outbox);

    RpcSession session(nullptr);
    RpcSessionData data; data.transport = transport;

    // push remap with captures [["export", 5]] calling greet("Bob") on the captured stub
    json captures = json::array({ json::array({"export", 5}) });
    json instrs = json::array({ json::array({"pipeline", -1, json::array({"greet"}), json::array({"Bob"})}) });
    json push = json::array({ "push", json::array({ "remap", 0, json::array(), captures, instrs }) });

    session.handleMessage(&data, push.dump());
    // Pull export 1 to get result placeholder (promise)
    json pull = json::array({ "pull", 1 });
    std::string res = session.handleMessage(&data, pull.dump());
    json msg = parse(res);

    bool ok = true;
    ok &= require(msg[0] == "resolve" && msg[1] == 1, "client-call: resolve for export 1");
    ok &= require(msg[2].is_array() && msg[2].size() == 2 && msg[2][0] == "promise", "client-call: payload is promise expr");
    int promiseId = msg[2][1];
    ok &= require(promiseId < 0, "client-call: promise export id is negative");

    // Outbox should have two messages: push and pull
    ok &= require(outbox.size() == 2, "client-call: push+pull sent");
    json m0 = parse(outbox[0]);
    json m1 = parse(outbox[1]);
    ok &= require(m0[0] == "push" && m0[1][0] == "pipeline" && m0[1][1] == 5, "client-call: push pipeline exportId 5");
    ok &= require(m1[0] == "pull" && m1[1] == 1, "client-call: pull importId 1");

    // Now simulate client resolving our importId 1, verify server forwards resolve for promiseId.
    std::string rel = session.handleMessage(&data, json::array({"resolve", 1, "Hello, Bob!"}).dump());
    ok &= require(parse(rel) == json::array({"release", 1, 1}), "client-call: release for import 1");

    ok &= require(outbox.size() == 3, "client-call: forwarded resolve sent");
    json m2 = parse(outbox[2]);
    ok &= require(m2[0] == "resolve" && m2[1] == promiseId && m2[2] == "Hello, Bob!", "client-call: resolve forwarded");

    return ok;
}

int main()
{
    int failed = 0;
    failed += !testClientCallPathViaRemap();
    if (failed == 0)
    {
        std::cout << "All client-call path tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " client-call path test(s) failed" << std::endl;
    return 1;
}
