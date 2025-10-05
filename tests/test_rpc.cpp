#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>

using json = nlohmann::json;
using capnwebcpp::RpcTarget;
using capnwebcpp::RpcSession;
using capnwebcpp::RpcSessionData;

struct TestTarget : public RpcTarget
{
    TestTarget()
    {
        // echo(name) -> string
        method("echo", [](const json& args)
        {
            std::string name;
            if (args.is_array() && !args.empty() && args[0].is_string())
                name = args[0];
            else if (args.is_string())
                name = args.get<std::string>();
            else
                name = "";
            return json("Hello, " + name + "!");
        });

        // makeUser() -> { id: "u1" }
        method("makeUser", [](const json&)
        {
            return json{
                {"id", "u1"}
            };
        });

        // getProfile(userId) -> { id: userId, bio: "ok" }
        method("getProfile", [](const json& args)
        {
            std::string id = args.is_array() && !args.empty() ? args[0].get<std::string>() : std::string();
            return json{
                {"id", id},
                {"bio", "ok"}
            };
        });

        // getExportStub() -> special object that devaluator will turn into ["export", -1]
        method("getExportStub", [](const json&)
        {
            return json{ {"$export", true} };
        });

        // getExportPromise() -> special object that devaluator will turn into ["promise", -1]
        // and resolves to {"ok":true}
        method("getExportPromise", [](const json&)
        {
            return json{ {"$promise", json{ {"ok", true} }} };
        });
    }
};

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

static bool testSimpleCall()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data;
    data.target = target;

    // push: ["push", ["pipeline", 0, ["echo"], ["World"]]]
    json push = json::array({
        "push",
        json::array({ "pipeline", 0, json::array({"echo"}), json::array({"World"}) })
    });
    session.handleMessage(&data, push.dump());

    // pull: ["pull", 1]
    json pull = json::array({ "pull", 1 });
    std::string res = session.handleMessage(&data, pull.dump());
    json msg = parse(res);

    bool ok = true;
    ok &= require(msg.is_array(), "simple call: response is array");
    ok &= require(msg.size() == 3, "simple call: response has 3 elements");
    ok &= require(msg[0] == "resolve", "simple call: response is resolve");
    ok &= require(msg[1] == 1, "simple call: export id is 1");
    ok &= require(msg[2] == "Hello, World!", "simple call: payload matches");
    return ok;
}

static bool testPipelineArgResolution()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data;
    data.target = target;

    // push1: makeUser()
    json push1 = json::array({
        "push",
        json::array({ "pipeline", 0, json::array({"makeUser"}) })
    });
    session.handleMessage(&data, push1.dump());

    // push2: getProfile([ ["pipeline", 1, ["id"] ] ])
    json argRef = json::array({ "pipeline", 1, json::array({"id"}) });
    json push2 = json::array({
        "push",
        json::array({ "pipeline", 0, json::array({"getProfile"}), json::array({ argRef }) })
    });
    session.handleMessage(&data, push2.dump());

    // pull export 2
    std::string res = session.handleMessage(&data, json::array({"pull", 2}).dump());
    json msg = parse(res);
    bool ok = true;
    ok &= require(msg[0] == "resolve", "pipeline: resolve");
    ok &= require(msg[1] == 2, "pipeline: export id 2");
    ok &= require(msg[2].is_object(), "pipeline: payload is object");
    ok &= require(msg[2]["id"] == "u1", "pipeline: id propagated");
    ok &= require(msg[2]["bio"] == "ok", "pipeline: bio ok");
    return ok;
}

static bool testReleaseThenPull()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data;
    data.target = target;

    // push1: makeUser()
    json push1 = json::array({
        "push",
        json::array({ "pipeline", 0, json::array({"makeUser"}) })
    });
    session.handleMessage(&data, push1.dump());

    // release export 1
    session.handleMessage(&data, json::array({"release", 1, 1}).dump());

    // pull export 1 -> expect reject ExportNotFound
    std::string res = session.handleMessage(&data, json::array({"pull", 1}).dump());
    json msg = parse(res);
    bool ok = true;
    ok &= require(msg[0] == "reject", "release: reject");
    ok &= require(msg[1] == 1, "release: id 1");
    ok &= require(msg[2].is_array() && msg[2].size() >= 3 && msg[2][0] == "error" && msg[2][1] == "ExportNotFound",
                 "release: ExportNotFound error");
    return ok;
}

static bool testNegativeExportEmission()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data;
    data.target = target;

    // push: getExportStub()
    json push = json::array({
        "push",
        json::array({ "pipeline", 0, json::array({"getExportStub"}) })
    });
    session.handleMessage(&data, push.dump());

    // pull export 1
    std::string res = session.handleMessage(&data, json::array({"pull", 1}).dump());
    json msg = parse(res);
    bool ok = true;
    ok &= require(msg[0] == "resolve", "negative export: resolve");
    ok &= require(msg[1] == 1, "negative export: export id 1");
    ok &= require(msg[2].is_array() && msg[2].size() == 2 && msg[2][0] == "export" && msg[2][1].is_number_integer(),
                 "negative export: payload is [export, id]");
    ok &= require(msg[2][1] < 0, "negative export: id is negative");
    return ok;
}

static bool testPromiseExportEmissionAndPull()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data;
    data.target = target;

    // push: getExportPromise()
    json push = json::array({
        "push",
        json::array({ "pipeline", 0, json::array({"getExportPromise"}) })
    });
    session.handleMessage(&data, push.dump());

    // pull export 1
    std::string res = session.handleMessage(&data, json::array({"pull", 1}).dump());
    json msg = parse(res);
    bool ok = true;
    ok &= require(msg[0] == "resolve", "promise export: resolve frame");
    ok &= require(msg[1] == 1, "promise export: id 1");
    ok &= require(msg[2].is_array() && msg[2][0] == "promise" && msg[2][1].is_number_integer() && msg[2][1] < 0,
                 "promise export: payload is [promise, negId]");

    int promiseId = msg[2][1];

    // now pull the promise export id -> should resolve to {"ok":true}
    std::string res2 = session.handleMessage(&data, json::array({"pull", promiseId}).dump());
    json msg2 = parse(res2);
    ok &= require(msg2[0] == "resolve", "promise export: inner resolve");
    ok &= require(msg2[1] == promiseId, "promise export: inner id match");
    ok &= require(msg2[2].is_object() && msg2[2]["ok"] == true, "promise export: resolution payload");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testSimpleCall();
    failed += !testPipelineArgResolution();
    failed += !testReleaseThenPull();
    failed += !testNegativeExportEmission();
    failed += !testPromiseExportEmissionAndPull();

    if (failed == 0)
    {
        std::cout << "All tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " test(s) failed" << std::endl;
    return 1;
}
