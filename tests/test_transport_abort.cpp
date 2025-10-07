#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <capnwebcpp/transport.h>
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

class TestTransport : public RpcTransport
{
public:
    void send(const std::string& message) override
    {
        out.push_back(message);
    }

    void abort(const std::string& reason) override
    {
        aborted = true;
        abortReason = reason;
    }

    std::vector<std::string> out;
    bool aborted = false;
    std::string abortReason;
};

static bool testPumpMessageAbortsTransportOnSessionAbort()
{
    RpcSession session(nullptr);
    RpcSessionData data;
    TestTransport transport;

    std::string abortMsg = json::array({ "abort", "bye" }).dump();
    pumpMessage(session, &data, transport, abortMsg);

    bool ok = true;
    ok &= require(session.isAborted(), "pump abort: session marked aborted");
    ok &= require(transport.aborted, "pump abort: transport aborted");
    ok &= require(transport.abortReason.size() > 0, "pump abort: reason set");
    ok &= require(data.exports.empty() && data.imports.empty(), "pump abort: tables cleared");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testPumpMessageAbortsTransportOnSessionAbort();
    if (failed == 0)
    {
        std::cout << "All transport abort tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " transport abort test(s) failed" << std::endl;
    return 1;
}

