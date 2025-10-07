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

static bool testBatchRejectsExportCaptureGetWithoutTransport()
{
    RpcSession session(nullptr);
    RpcSessionData data; // no transport set: simulates HTTP batch path

    // Construct a batch with a remap that captures an export and performs a property get.
    std::string body;
    json captures = json::array({ json::array({"export", 7}) });
    json instrs = json::array({ json::array({"get", -1, json::array({"version"})}) });
    body += json::array({ "push", json::array({ "remap", 0, json::array(), captures, instrs }) }).dump();
    body += "\n";
    body += json::array({ "pull", 1 }).dump();

    auto responses = processBatch(session, &data, body);
    bool ok = true;
    ok &= require(responses.size() == 1, "batch export-capture get: one response");
    auto m = parse(responses[0]);
    ok &= require(m.is_array() && m.size() == 3, "batch export-capture get: response shape");
    ok &= require(m[0] == "reject" && m[1] == 1, "batch export-capture get: reject id 1");
    ok &= require(m[2].is_array() && m[2].size() >= 3 && m[2][0] == "error" && m[2][1] == "MethodError",
                 "batch export-capture get: MethodError error tuple");
    ok &= require(m[2][2].is_string() && m[2][2].get<std::string>().find("no transport") != std::string::npos,
                 "batch export-capture get: error message mentions no transport");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testBatchRejectsExportCaptureGetWithoutTransport();
    if (failed == 0)
    {
        std::cout << "All batch export-capture get tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " batch export-capture get test(s) failed" << std::endl;
    return 1;
}

