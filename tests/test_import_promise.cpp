#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/client_stub.h>
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

struct PromiseEchoTarget : public RpcTarget
{
    PromiseEchoTarget(RpcSession* s, RpcSessionData* d) : session(s), data(d)
    {
        // echoPromise(promiseStub) -> map to ["promise", negId] by linking import->promise
        method("echoPromise", [this](const json& args)
        {
            json v = args.is_array() && !args.empty() ? args[0] : json();
            if (isClientPromiseStub(v))
            {
                return awaitClientPromiseAsResult(session, data, v);
            }
            return json("no-promise");
        });
    }
    RpcSession* session;
    RpcSessionData* data;
};

static json parse(const std::string& s) { return json::parse(s); }

static bool testPromiseStubReturnMappingAndForward()
{
    std::vector<std::string> outbox;
    auto transport = std::make_shared<AccumTransport>(outbox);

    RpcSession session(nullptr);
    RpcSessionData data; 
    auto target = std::make_shared<PromiseEchoTarget>(&session, &data);
    data.target = target;
    data.transport = transport;

    // push: echoPromise(["promise", 5])
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"echoPromise"}), json::array({ json::array({"promise", 5}) })})}).dump());
    // pull export 1
    json msg = parse(session.handleMessage(&data, json::array({"pull", 1}).dump()));
    bool ok = true;
    ok &= require(msg[0] == "resolve" && msg[1] == 1, "promise stub: resolve id 1");
    ok &= require(msg[2].is_array() && msg[2][0] == "promise" && msg[2][1].is_number_integer() && msg[2][1] < 0,
                  "promise stub: mapped to [promise, negId]");
    int promiseNeg = msg[2][1];

    // Simulate client resolving import 5; expect forwarded resolve to negId
    (void)session.handleMessage(&data, json::array({"resolve", 5, "OK"}).dump());
    ok &= require(outbox.size() >= 1, "promise stub: forwarded emitted");
    json fwd = parse(outbox.back());
    ok &= require(fwd[0] == "resolve" && fwd[1] == promiseNeg && fwd[2] == "OK", "promise stub: forward resolve to negId");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testPromiseStubReturnMappingAndForward();
    if (failed == 0)
    {
        std::cout << "All import promise tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " import promise test(s) failed" << std::endl;
    return 1;
}
