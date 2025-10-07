#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/transports/message_port_transport.h>

using json = nlohmann::json;
using namespace capnwebcpp;

class ServerApi : public RpcTarget
{
public:
    ServerApi()
    {
        method("info", [](const json&){ return json({{"role","server"}}); });
    }
};

int main()
{
    MessageChannel channel;

    auto api = std::make_shared<ServerApi>();
    RpcSession session(api);
    RpcSessionData data; data.target = api;
    // Persist a transport for server-initiated client calls
    data.transport = std::make_shared<MessagePortTransport>(&channel.port1);

    // Server-side handler for messages arriving from the client on port1
    channel.port1.setHandler([&](const std::string& message)
    {
        try
        {
            MessagePortTransport transport(&channel.port1);
            pumpMessage(session, &data, transport, message);
            session.processTasks();
        }
        catch (const std::exception& e)
        {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
    });

    // Minimal client-side RPC shim: respond to push/pull and print forwarded promise resolves.
    std::string lastMethod;
    json lastArgs;
    channel.port2.setHandler([&](const std::string& message)
    {
        auto m = json::parse(message);
        if (!m.is_array() || m.empty() || !m[0].is_string()) return;
        std::string tag = m[0];
        if (tag == "push")
        {
            // Expect ["push", ["pipeline", exportId, ["method"], args?]]
            auto pl = m[1];
            if (pl.is_array() && pl.size() >= 3 && pl[0] == "pipeline")
            {
                auto path = pl[2];
                if (path.is_array() && !path.empty() && path[0].is_string())
                {
                    lastMethod = path[0];
                }
                if (pl.size() >= 4)
                {
                    lastArgs = pl[3];
                }
                else
                {
                    lastArgs = json::array();
                }
            }
        }
        else if (tag == "pull")
        {
            // Compute a demo result and send resolve to the server's promise import id.
            int importId = m[1];
            json result;
            if (lastMethod == "greet" && lastArgs.is_array() && !lastArgs.empty())
            {
                result = json("Hello from client: " + std::string(lastArgs[0]));
            }
            else if (lastMethod == "version")
            {
                result = json::array({"version","1.2.3"});
            }
            else
            {
                result = json("(no-op)");
            }
            channel.port2.postMessage(json::array({"resolve", importId, result}).dump());
        }
        else if (tag == "resolve")
        {
            // Server forwarded our result to a promise; just print it.
            std::cout << "Client received forwarded: " << message << std::endl;
        }
    });

    // Demonstrate server-to-client method call
    int promiseId1 = session.callClientMethod(&data, /*exportId=*/7, /*method=*/"greet", json::array({"Alice"}));
    std::cout << "Server exported promise id: " << promiseId1 << std::endl;

    // Demonstrate server-to-client get call
    int promiseId2 = session.callClient(&data, /*exportId=*/7, json::array({"version"}));
    std::cout << "Server exported promise id: " << promiseId2 << std::endl;

    return 0;
}

