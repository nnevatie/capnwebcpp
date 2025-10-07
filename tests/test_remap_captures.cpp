#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
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

static json parse(const std::string& s)
{
    return json::parse(s);
}

// Verify that remap captures distinguish between ["import", id] and ["export", id].
// Currently, ["export", id] captures are not supported and should produce a MethodError.
static bool testRemapExportCaptureRejects()
{
    auto target = std::make_shared<RpcTarget>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    // push remap with captures = [["export", 123]] and a simple pipeline instruction using the capture.
    json captures = json::array({ json::array({"export", 123}) });
    json instrs = json::array({ json::array({"pipeline", -1, json::array({"echo"}), json::array({"X"})}) });
    json push = json::array({ "push", json::array({ "remap", 0, json::array(), captures, instrs }) });

    session.handleMessage(&data, push.dump());

    // pull export 1, expect reject(MethodError,...)
    std::string resp = session.handleMessage(&data, json::array({"pull", 1}).dump());
    json msg = parse(resp);

    bool ok = true;
    ok &= require(msg.is_array() && msg.size() == 3, "remap export capture: response shape");
    ok &= require(msg[0] == "reject" && msg[1] == 1, "remap export capture: reject id 1");
    ok &= require(msg[2].is_array() && msg[2].size() >= 3 && msg[2][0] == "error" && msg[2][1] == "MethodError",
                 "remap export capture: MethodError");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testRemapExportCaptureRejects();
    if (failed == 0)
    {
        std::cout << "All remap capture tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " remap capture test(s) failed" << std::endl;
    return 1;
}

