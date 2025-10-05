#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>

#include <App.h>

#include "capnwebcpp/rpc_session.h"
#include "capnwebcpp/transport.h"
#include "capnwebcpp/transports/uws_websocket_transport.h"
#include "capnwebcpp/transports/accum_transport.h"
#include "capnwebcpp/batch.h"

namespace capnwebcpp
{

// Helper to set up RPC endpoint with uWebSockets (WebSocket + HTTP POST).
template<typename App>
void setupRpcEndpoint(App& app, const std::string& path, std::shared_ptr<RpcTarget> target)
{
    auto session = std::make_shared<RpcSession>(target);

    // WebSocket endpoint.
    app.template ws<RpcSessionData>(path,
    {
        .open = [session, target](auto* ws)
        {
            auto* userData = ws->getUserData();
            userData->target = target;
            session->onOpen(userData);
        },
        .message = [session](auto* ws, std::string_view message, uWS::OpCode)
        {
            auto* userData = ws->getUserData();
            try
            {
                UwsWebSocketTransport<decltype(ws)> transport(ws);
                pumpMessage(*session, userData, transport, std::string(message));
                session->processTasks();
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error processing message: " << e.what() << std::endl;
            }
        },
        .drain = [](auto*)
        {
        },
        .close = [session](auto* ws, int, std::string_view)
        {
            auto* userData = ws->getUserData();
            session->onClose(userData);
        }
    });

    // HTTP POST endpoint for batch RPC.
    app.post(path, [session, target](auto* res, auto*)
    {
        std::string body;

        res->onAborted([]()
        {
            std::cerr << "HTTP request aborted" << std::endl;
        });

        res->onData([res, session, target, body = std::move(body)](std::string_view data, bool isEnd) mutable
        {
            body.append(data);

            if (isEnd)
            {
                try
                {
                    RpcSessionData sessionData;
                    sessionData.target = target;
                    sessionData.exporter.reset();

                    std::vector<std::string> responses = processBatch(*session, &sessionData, body);

                    res->writeHeader("Content-Type", "text/plain");
                    res->writeHeader("Access-Control-Allow-Origin", "*");

                    std::string responseBody;
                    for (size_t i = 0; i < responses.size(); ++i)
                    {
                        if (i > 0) responseBody += "\n";
                        responseBody += responses[i];
                    }

                    // Ensure the session has drained outstanding pulls before ending.
                    (void)session->isDrained();
                    res->end(responseBody);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error processing HTTP POST: " << e.what() << std::endl;
                    res->writeHeader("Content-Type", "text/plain");
                    res->writeHeader("Access-Control-Allow-Origin", "*");
                    res->writeStatus("500 Internal Server Error");
                    res->end("Internal server error");
                }
            }
        });
    });

    // OPTIONS for CORS preflight.
    app.options(path, [](auto* res, auto*)
    {
        res->writeHeader("Access-Control-Allow-Origin", "*");
        res->writeHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
        res->writeHeader("Access-Control-Allow-Headers", "Content-Type");
        res->end();
    });
}

} // namespace capnwebcpp
