#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

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

// Target returns $export sentinel when calling getExportStub method.
struct ExportingTarget : public RpcTarget
{
    ExportingTarget()
    {
        method("getExportStub", [](const json&){ return json{ {"$export", true} }; });
    }
};

static json parse(const std::string& s) { return json::parse(s); }

static bool testReexportIncrementsRefcountAndReusesId()
{
    auto target = std::make_shared<ExportingTarget>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    // First push/pull
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"getExportStub"})})}).dump());
    json msg1 = parse(session.handleMessage(&data, json::array({"pull", 1}).dump()));
    bool ok = true;
    ok &= require(msg1[0] == "resolve" && msg1[2].is_array() && msg1[2][0] == "export", "reexport: first resolve export");
    int id1 = msg1[2][1];
    ok &= require(id1 < 0, "reexport: first export id negative");

    // Second push/pull should reuse the same export id and bump remoteRefcount.
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"getExportStub"})})}).dump());
    json msg2 = parse(session.handleMessage(&data, json::array({"pull", 2}).dump()));
    ok &= require(msg2[0] == "resolve" && msg2[2].is_array() && msg2[2][0] == "export", "reexport: second resolve export");
    int id2 = msg2[2][1];
    ok &= require(id2 == id1, "reexport: reused same export id");

    // After one release, export should still exist; after second release, it should be erased.
    session.handleMessage(&data, json::array({"release", id1, 1}).dump());
    ok &= require(data.exports.find(id1) != data.exports.end(), "reexport: entry still present after first release");
    session.handleMessage(&data, json::array({"release", id1, 1}).dump());
    ok &= require(data.exports.find(id1) == data.exports.end(), "reexport: entry removed after second release");

    return ok;
}

int main()
{
    int failed = 0;
    failed += !testReexportIncrementsRefcountAndReusesId();
    if (failed == 0)
    {
        std::cout << "All re-export tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " re-export test(s) failed" << std::endl;
    return 1;
}

