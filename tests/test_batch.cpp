#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/batch.h>

using json = nlohmann::json;
using namespace capnwebcpp;

struct TestTarget : public RpcTarget
{
    TestTarget()
    {
        method("echo", [](const json& args)
        {
            std::string name = args.is_array() && !args.empty() ? args[0].get<std::string>() : std::string();
            return json("Hello, " + name + "!");
        });

        method("makeUser", [](const json&)
        {
            return json{ {"id", "u1"} };
        });

        method("getProfile", [](const json& args)
        {
            std::string id = args.is_array() && !args.empty() ? args[0].get<std::string>() : std::string();
            return json{ {"id", id}, {"bio", "ok"} };
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

static bool testMultiLinePushPull()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    std::string body;
    body += json::array({"push", json::array({"pipeline", 0, json::array({"echo"}), json::array({"A"})})}).dump();
    body += "\n";
    body += json::array({"push", json::array({"pipeline", 0, json::array({"echo"}), json::array({"B"})})}).dump();
    body += "\n";
    body += json::array({"pull", 1}).dump();
    body += "\n";
    body += json::array({"pull", 2}).dump();

    auto responses = processBatch(session, &data, body);
    bool ok = true;
    ok &= require(responses.size() == 2, "batch: two responses");
    auto m1 = parse(responses[0]);
    auto m2 = parse(responses[1]);
    ok &= require(m1[0] == "resolve" && m1[1] == 1 && m1[2] == "Hello, A!", "batch: first resolve");
    ok &= require(m2[0] == "resolve" && m2[1] == 2 && m2[2] == "Hello, B!", "batch: second resolve");
    ok &= require(session.isDrained(), "batch: drained");
    return ok;
}

static bool testPipelineWithinBatch()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    std::string body;
    body += json::array({"push", json::array({"pipeline", 0, json::array({"makeUser"})})}).dump();
    body += "\n";
    // getProfile([ ["pipeline", 1, ["id"] ] ])
    body += json::array({"push", json::array({"pipeline", 0, json::array({"getProfile"}), json::array({ json::array({"pipeline", 1, json::array({"id"}) }) })})}).dump();
    body += "\n";
    body += json::array({"pull", 2}).dump();

    auto responses = processBatch(session, &data, body);
    bool ok = true;
    ok &= require(responses.size() == 1, "batch pipeline: one response");
    auto m = parse(responses[0]);
    ok &= require(m[0] == "resolve" && m[1] == 2 && m[2].is_object() && m[2]["id"] == "u1", "batch pipeline: resolve profile");
    ok &= require(session.isDrained(), "batch pipeline: drained");
    return ok;
}

static bool testRemapSimple()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    std::string body;
    // 1) push makeUser -> export 1
    body += json::array({"push", json::array({"pipeline", 0, json::array({"makeUser"})})}).dump();
    body += "\n";
    // 2) push remap: call getProfile(main, user.id) -> export 2
    json captures = json::array({ json::array({"import", 0}) });
    json instrs = json::array({
        json::array({"pipeline", -1, json::array({"getProfile"}), json::array({ json::array({"pipeline", 1, json::array({"id"}) }) })})
    });
    body += json::array({"push", json::array({"remap", 0, json::array(), captures, instrs})}).dump();
    body += "\n";
    // 3) pull export 2
    body += json::array({"pull", 2}).dump();

    auto responses = processBatch(session, &data, body);
    bool ok = true;
    ok &= require(responses.size() == 1, "remap: one response");
    auto m = parse(responses[0]);
    ok &= require(m[0] == "resolve" && m[1] == 2 && m[2].is_object() && m[2]["id"] == "u1", "remap: resolve profile");
    ok &= require(session.isDrained(), "remap: drained");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testMultiLinePushPull();
    failed += !testPipelineWithinBatch();
    failed += !testRemapSimple();
    if (failed == 0)
    {
        std::cout << "All batch tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " batch test(s) failed" << std::endl;
    return 1;
}
