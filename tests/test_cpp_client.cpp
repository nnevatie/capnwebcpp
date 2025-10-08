#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/batch.h>
#include <capnwebcpp/client_api.h>

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

struct HelloTarget : public RpcTarget
{
    HelloTarget()
    {
        method("hello", [](const json& args)
        {
            std::string name = args.is_array() && !args.empty() ? args[0].get<std::string>() : std::string();
            return json("Hello, " + name + "!");
        });
    }
};

static bool testClientCallsHello()
{
    auto target = std::make_shared<HelloTarget>();

    auto transport = std::make_shared<FuncBatchTransport>([target](const std::vector<std::string>& lines)
    {
        RpcSession session(target);
        RpcSessionData data; data.target = target;
        std::string body;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i > 0) body += "\n";
            body += lines[i];
        }
        return processBatch(session, &data, body);
    });

    RpcClient client(transport);
    json result = client.callMethod("hello", json::array({"World"}));
    return require(result.is_string() && result.get<std::string>() == "Hello, World!", "client hello");
}

int main()
{
    int failed = 0;
    failed += !testClientCallsHello();
    if (failed == 0)
    {
        std::cout << "All C++ client tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " C++ client test(s) failed" << std::endl;
    return 1;
}

