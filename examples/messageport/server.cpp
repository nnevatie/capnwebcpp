#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/transports/message_port_transport.h>

using json = nlohmann::json;
using namespace capnwebcpp;

class HelloServer : public RpcTarget
{
public:
    HelloServer()
    {
        method("hello", [](const json& args)
        {
            std::string name = args.is_array() && !args.empty() ? args[0].get<std::string>() : std::string("world");
            return json("Hello, " + name + "!");
        });
    }
};

int main()
{
    // Create an in-process MessageChannel with two connected ports.
    MessageChannel channel;

    auto target = std::make_shared<HelloServer>();
    RpcSession session(target);
    RpcSessionData data; data.target = target;

    // Server-side: handle messages arriving on port1 and send responses back via the same port.
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
            std::cerr << "Error: " << e.what() << std::endl;
        }
    });

    // Client-side: print any responses arriving on port2.
    channel.port2.setHandler([&](const std::string& message)
    {
        std::cout << "client received: " << message << std::endl;
    });

    // Simulate a client calling hello("World") over MessagePort.
    json push = json::array({ "push", json::array({ "pipeline", 0, json::array({"hello"}), json::array({"World"}) }) });
    channel.port2.postMessage(push.dump());

    // Request the result.
    json pull = json::array({ "pull", 1 });
    channel.port2.postMessage(pull.dump());

    return 0;
}

