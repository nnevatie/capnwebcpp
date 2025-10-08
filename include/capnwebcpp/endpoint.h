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
#include "capnwebcpp/serialize.h"

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
            // Persist a transport for out-of-band sends (client-call path).
            userData->transport = std::make_shared<UwsWebSocketTransport<decltype(ws)>>(ws);
            // Create a canonical local target hook for re-export parity.
            userData->localTargetHook = makeLocalTargetHook(target);
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
                // Attempt to notify the peer and local listeners about the fatal error.
                try
                {
                    UwsWebSocketTransport<decltype(ws)> transport(ws);
                    auto err = serialize::makeError("ServerError", std::string(e.what()));
                    transport.send(session->buildAbort(err));
                    transport.abort("server error");
                }
                catch (...) {}
                session->markAborted(userData, std::string(e.what()));
            }
        },
        .drain = [](auto*)
        {
        },
        .close = [session](auto* ws, int, std::string_view)
        {
            auto* userData = ws->getUserData();
            // Best-effort: emit pending releases before closing.
            try
            {
                UwsWebSocketTransport<decltype(ws)> transport(ws);
                session->emitPendingReleases(userData, transport);
            }
            catch (...) {}
            session->onClose(userData);
        }
    });

    // HTTP POST endpoint for batch RPC.
    app.post(path, [target](auto* res, auto*)
    {
        std::string body;

        res->onAborted([]()
        {
            std::cerr << "HTTP request aborted" << std::endl;
        });

        res->onData([res, target, body = std::move(body)](std::string_view data, bool isEnd) mutable
        {
            body.append(data);

            if (isEnd)
            {
                try
                {
                    // Use a fresh session per HTTP batch to avoid cross-request state.
                    RpcSession sessionLocal(target);
                    RpcSessionData sessionData;
                    sessionData.target = target;
                    sessionData.exporter.reset();

                    // Accumulate all outbound messages using a transport adapter, to match capnweb's
                    // batch semantics where the server returns all responses at once after drain().
                    std::vector<std::string> outbox;
                    AccumTransport transport(outbox);

                    std::istringstream stream(body);
                    std::string line;
                    while (std::getline(stream, line))
                    {
                        if (line.empty()) continue;
                        pumpMessage(sessionLocal, &sessionData, transport, line);
                        sessionLocal.processTasks();
                    }

                    // Ensure the session has drained outstanding pulls before ending; then return
                    // all accumulated messages as newline-delimited body.
                    sessionLocal.drain(&sessionData);

                    res->writeHeader("Content-Type", "text/plain");
                    res->writeHeader("Access-Control-Allow-Origin", "*");

                    std::string responseBody;
                    for (size_t i = 0; i < outbox.size(); ++i)
                    {
                        if (i > 0) responseBody += "\n";
                        responseBody += outbox[i];
                    }

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
