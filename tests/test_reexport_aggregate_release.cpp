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

struct ExportingTarget : public RpcTarget
{
    ExportingTarget()
    {
        method("getExportStub", [](const json&){ return json{ {"$export", true} }; });
    }
};

static json parse(const std::string& s) { return json::parse(s); }

static bool testAggregateReleaseRemovesEntry()
{
    auto target = std::make_shared<ExportingTarget>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    // Export the same stub three times; expect same negative id each time and refcount increments.
    int lastExportId = 0;
    for (int i = 0; i < 3; ++i)
    {
        session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"getExportStub"})})}).dump());
        json msg = parse(session.handleMessage(&data, json::array({"pull", i + 1}).dump()));
        if (!require(msg[0] == "resolve" && msg[2].is_array() && msg[2][0] == "export", "agg: resolve export")) return false;
        int id = msg[2][1];
        if (i == 0)
        {
            lastExportId = id;
        }
        else
        {
            if (!require(id == lastExportId, "agg: reused same export id")) return false;
        }
    }

    // Verify refcount is 3.
    auto it = data.exports.find(lastExportId);
    bool ok = true;
    ok &= require(it != data.exports.end(), "agg: entry exists");
    if (it != data.exports.end())
    {
        ok &= require(it->second.remoteRefcount == 3, "agg: remoteRefcount == 3");
    }

    // Single release with count 3 should erase the entry.
    session.handleMessage(&data, json::array({"release", lastExportId, 3}).dump());
    ok &= require(data.exports.find(lastExportId) == data.exports.end(), "agg: entry removed after aggregated release");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testAggregateReleaseRemovesEntry();
    if (failed == 0)
    {
        std::cout << "All re-export aggregated release tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " re-export aggregated release test(s) failed" << std::endl;
    return 1;
}

