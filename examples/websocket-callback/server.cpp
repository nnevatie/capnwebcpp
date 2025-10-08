#include <iostream>
#include <string>
#include <optional>

#include <nlohmann/json.hpp>

#include <App.h>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/file_endpoint.h>
#include <capnwebcpp/export_id.h>
#include <capnwebcpp/transports/uws_websocket_transport.h>

using json = nlohmann::json;
using namespace capnwebcpp;

class CallbackServer : public RpcTarget
{
public:
    void setSessionContext(RpcSession* s, RpcSessionData* d)
    {
        session = s;
        data = d;
    }

    CallbackServer()
    {
        // register(clientExport)
        // clientExport is expected to be an ["export", id] tuple from the client.
        method("register", [this](const json& args)
        {
            if (!session || !data)
                return json({{"error","no session"}});
            if (!args.is_array() || args.empty())
                return json({{"error","invalid args"}});

            auto id = extractExportId(args[0]);
            if (!id)
                return json({{"error","expected export or stub"}});
            int clientExportId = *id;

            // Call client method greet(name) and also perform a get for version.
            try
            {
                int p1 = session->callClientMethod(data, clientExportId, "greet", json::array({"from server"}));
                int p2 = session->callClient(data, clientExportId, json::array({"version"}));
                (void)p1; (void)p2; // For demo, we don't return these to client.
            }
            catch (const std::exception& e)
            {
                return json({{"error", e.what()}});
            }

            return json({{"ok", true}});
        });
    }

private:
    RpcSession* session = nullptr;
    RpcSessionData* data = nullptr;
};

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        const auto port = 8000;
        const std::string staticRoot = argv[1];

        auto target = std::make_shared<CallbackServer>();
        auto session = std::make_shared<RpcSession>(target);

        uWS::App app;

        app.ws<RpcSessionData>("/api",
        {
            .open = [session, target](auto* ws)
            {
                auto* userData = ws->getUserData();
                userData->target = target;
                userData->transport = std::make_shared<UwsWebSocketTransport<decltype(ws)>>(ws);
                session->onOpen(userData);
                // Provide session context to the target for server->client calls.
                target->setSessionContext(session.get(), userData);
            },
            .message = [session, target](auto* ws, std::string_view message, uWS::OpCode)
            {
                auto* userData = ws->getUserData();
                try
                {
                    UwsWebSocketTransport<decltype(ws)> transport(ws);
                    // Update context in case of per-connection change.
                    target->setSessionContext(session.get(), userData);
                    pumpMessage(*session, userData, transport, std::string(message));
                    session->processTasks();
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error processing message: " << e.what() << std::endl;
                }
            },
            .close = [session](auto* ws, int, std::string_view)
            {
                auto* userData = ws->getUserData();
                session->onClose(userData);
            }
        });

        setupFileEndpoint(app, "/static/", staticRoot);

        app.listen(port, [port](auto* token)
        {
            if (token)
                std::cout << "Listening on port " << port << std::endl;
            else
                std::cerr << "Failed to listen on port " << port << std::endl;
        }).run();
    }
    return 0;
}
