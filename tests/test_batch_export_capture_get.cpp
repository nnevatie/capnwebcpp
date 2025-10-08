#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

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

static json parse(const std::string& s)
{
    return json::parse(s);
}

static bool testBatchAllowsExportCaptureGetWithBatchTransport()
{
    RpcSession session(nullptr);
    RpcSessionData data; // batch path will supply an accumulating transport

    // Construct a batch with a remap that captures an export and performs a property get.
    std::string body;
    json captures = json::array({ json::array({"export", 7}) });
    json instrs = json::array({ json::array({"get", -1, json::array({"version"})}) });
    body += json::array({ "push", json::array({ "remap", 0, json::array(), captures, instrs }) }).dump();
    body += "\n";
    body += json::array({ "pull", 1 }).dump();

    auto outbox = processBatch(session, &data, body);
    bool ok = true;
    // Expect at least: push (to client), pull (for import), resolve (promise), release (of captured export)
    ok &= require(outbox.size() >= 3, "batch export-capture get: has multiple frames");
    // First should be push to client
    auto msg0 = parse(outbox[0]);
    ok &= require(msg0.is_array() && msg0.size() >= 2 && msg0[0] == "push", "batch export-capture get: first is push");
    ok &= require(msg0[1].is_array() && msg0[1].size() >= 3 && msg0[1][0] == "pipeline", "batch export-capture get: inner pipeline");
    ok &= require(msg0[1][1] == 7, "batch export-capture get: pipeline export id 7");
    ok &= require(msg0[1][2].is_array() && msg0[1][2].size() >= 1 && msg0[1][2][0] == "version",
                 "batch export-capture get: path ['version']");

    // Second should be pull for import id allocated by server (positive int)
    auto msg1 = parse(outbox[1]);
    ok &= require(msg1.is_array() && msg1.size() >= 2 && msg1[0] == "pull", "batch export-capture get: second is pull");
    ok &= require(msg1[1].is_number_integer() && msg1[1].get<int>() > 0, "batch export-capture get: pull positive import id");

    // Somewhere should be resolve for export id 1 with a promise expression
    bool foundResolve = false;
    bool foundRelease = false;
    for (const auto& line : outbox)
    {
        auto mm = parse(line);
        if (mm.is_array() && mm.size() >= 3 && mm[0] == "resolve" && mm[1] == 1 && mm[2].is_array())
        {
            foundResolve = (mm[2][0] == "promise" && mm[2].size() >= 2 && mm[2][1].is_number_integer());
        }
        if (mm.is_array() && mm.size() >= 3 && mm[0] == "release" && mm[1] == 7)
        {
            foundRelease = true;
        }
    }
    ok &= require(foundResolve, "batch export-capture get: found resolve with promise expression");
    ok &= require(foundRelease, "batch export-capture get: found release for captured export");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testBatchAllowsExportCaptureGetWithBatchTransport();
    if (failed == 0)
    {
        std::cout << "All batch export-capture get tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " batch export-capture get test(s) failed" << std::endl;
    return 1;
}
