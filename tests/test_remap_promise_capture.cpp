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

static bool testRemapExportPromiseAwait()
{
    std::vector<std::string> outbox;
    auto transport = std::make_shared<AccumTransport>(outbox);

    RpcSession session(nullptr);
    RpcSessionData data; data.transport = transport;

    // Build remap that awaits a captured export promise: captures = [["export", 7]], instr = [["get", -1, []]]
    json captures = json::array({ json::array({"export", 7}) });
    json instrs = json::array({ json::array({"get", -1, json::array()}) });
    json push = json::array({ "push", json::array({ "remap", 0, json::array(), captures, instrs }) });

    session.handleMessage(&data, push.dump());
    // Pull export 1 -> expect a promise expression in the result
    json msg = parse(session.handleMessage(&data, json::array({"pull", 1}).dump()));
    bool ok = true;
    ok &= require(msg[0] == "resolve" && msg[1] == 1, "remap promise: resolve id 1");
    ok &= require(msg[2].is_array() && msg[2][0] == "promise" && msg[2][1].is_number_integer() && msg[2][1] < 0,
                  "remap promise: payload is [promise, negId]");
    int promiseNeg = msg[2][1];

    // Expect outbox contains push+pull to client for captured export id 7
    ok &= require(outbox.size() >= 2, "remap promise: push+pull sent");
    json m0 = parse(outbox[0]);
    json m1 = parse(outbox[1]);
    ok &= require(m0[0] == "push" && m0[1][0] == "pipeline" && m0[1][1] == 7, "remap promise: push to export 7");
    ok &= require(m1[0] == "pull" && m1[1] == 1, "remap promise: pull import 1");

    // Simulate the client resolving the import id 1; expect forwarded resolve to promiseNeg
    (void)session.handleMessage(&data, json::array({"resolve", 1, "OK"}).dump());
    ok &= require(outbox.size() >= 3, "remap promise: forwarded resolve present");
    json fwd = parse(outbox.back());
    ok &= require(fwd[0] == "resolve" && fwd[1] == promiseNeg && fwd[2] == "OK", "remap promise: forward matches");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testRemapExportPromiseAwait();
    if (failed == 0)
    {
        std::cout << "All remap promise capture tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " remap promise capture test(s) failed" << std::endl;
    return 1;
}

