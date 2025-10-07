#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/batch.h>

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

struct TestTarget : public RpcTarget
{
    TestTarget()
    {
        method("echo", [](const json& args)
        {
            std::string name = args.is_array() && !args.empty() ? args[0].get<std::string>() : std::string();
            return json("Hello, " + name + "!");
        });
    }
};

static bool testDrainAfterBatch()
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
    ok &= require(responses.size() == 2, "drain: got two responses");

    // Explicitly drain any remaining microtasks/pulls.
    session.drain(&data);
    ok &= require(session.isDrained(), "drain: session drained");
    return ok;
}

static bool testGetStats()
{
    auto target = std::make_shared<TestTarget>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    auto stats0 = session.getStats(&data);
    bool ok = true;
    ok &= require(stats0.imports == 0 && stats0.exports == 0, "stats: initially 0/0");

    // Push one call, creating export id 1.
    json push = json::array({
        "push",
        json::array({ "pipeline", 0, json::array({"echo"}), json::array({"World"}) })
    });
    session.handleMessage(&data, push.dump());

    auto stats1 = session.getStats(&data);
    ok &= require(stats1.exports == 1, "stats: one export after push");

    // Seed importer with two entries to simulate peer-believed remote refs.
    data.importer.setRefcounts(100, 1, 1);
    data.importer.setRefcounts(101, 2, 1);
    auto stats2 = session.getStats(&data);
    ok &= require(stats2.imports == 2, "stats: two imports after seeding");

    // Drain pending microtasks and verify still consistent.
    session.drain(&data);
    auto stats3 = session.getStats(&data);
    ok &= require(stats3.exports >= 1, "stats: exports present after drain");
    ok &= require(stats3.imports == 2, "stats: imports remain after drain");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testDrainAfterBatch();
    failed += !testGetStats();
    if (failed == 0)
    {
        std::cout << "All drain/stats tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " drain/stats test(s) failed" << std::endl;
    return 1;
}

