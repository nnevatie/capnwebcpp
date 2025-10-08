#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/client_stub.h>

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

struct StubEchoTarget : public RpcTarget
{
    StubEchoTarget(RpcSession* s, RpcSessionData* d) : session(s), data(d)
    {
        // returnStub(stub) -> return the same stub so that response devaluation yields ["import", id]
        method("returnStub", [this](const json& args)
        {
            json stub = args.is_array() && !args.empty() ? args[0] : json();
            return stub;
        });

        // callStub(stub) -> initiate a client call greet("X") and return "ok" (we don't await)
        method("callStub", [this](const json& args)
        {
            json stub = args.is_array() && !args.empty() ? args[0] : json();
            if (isClientStub(stub))
            {
                (void)callClientStubMethod(session, data, stub, "greet", json::array({"X"}));
                return json("ok");
            }
            return json("no-stub");
        });
    }

    RpcSession* session;
    RpcSessionData* data;
};

static json parse(const std::string& s) { return json::parse(s); }

static bool testImportStubReturnMapping()
{
    RpcSession session(nullptr);
    RpcSessionData data; data.target = std::make_shared<StubEchoTarget>(&session, &data);

    // push: returnStub(["export", 5])
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"returnStub"}), json::array({ json::array({"export", 5}) })})}).dump());
    // pull
    json msg = parse(session.handleMessage(&data, json::array({"pull", 1}).dump()));
    bool ok = true;
    ok &= require(msg[0] == "resolve" && msg[1] == 1, "import stub return: resolve id 1");
    ok &= require(msg[2].is_array() && msg[2][0] == "import" && msg[2][1] == 5, "import stub return: mapped to [import,5]");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testImportStubReturnMapping();
    if (failed == 0)
    {
        std::cout << "All import stub tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " import stub test(s) failed" << std::endl;
    return 1;
}

