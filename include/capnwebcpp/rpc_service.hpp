#pragma once

#include <memory>
#include <string>
#include <sstream>
#include <vector>
#include <functional>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <App.h>

namespace capnwebcpp
{

using json = nlohmann::json;

// Forward declarations
class RpcSession;

// RpcTarget is the base class for server implementations
class RpcTarget
{
public:
    virtual ~RpcTarget() = default;

    // This method is called by the RPC session to dispatch method calls
    virtual json dispatch(const std::string& method, const json& args)
    {
        auto it = methods.find(method);
        if (it != methods.end())
            return it->second(args);

        throw std::runtime_error("Method not found: " + method);
    }

protected:
    // Helper to register methods
    void method(const std::string& name, std::function<json(const json&)> handler)
    {
        methods[name] = handler;
    }

private:
    std::unordered_map<std::string, std::function<json(const json&)>> methods;
};

// Internal data associated with each WebSocket connection
struct RpcSessionData
{
    std::unordered_map<int, json> pendingResults;
    std::unordered_map<int, json> pendingOperations;
    int nextExportId = 1;
    std::shared_ptr<RpcTarget> target;
};

// RpcSession handles the Cap'n Web RPC protocol for a WebSocket connection
class RpcSession
{
public:
    RpcSession(std::shared_ptr<RpcTarget> target) : target(target) {}

    // Handle incoming WebSocket message
    std::string handleMessage(RpcSessionData* sessionData, const std::string& message);

    // Called when a new WebSocket connection is opened
    void onOpen(RpcSessionData* sessionData);

    // Called when a WebSocket connection is closed
    void onClose(RpcSessionData* sessionData);

private:
    std::shared_ptr<RpcTarget> target;

    void handlePush(RpcSessionData* sessionData, const json& pushData);
    json handlePull(RpcSessionData* sessionData, int exportId);
    void handleRelease(RpcSessionData* sessionData, int exportId, int refcount);
    void handleAbort(RpcSessionData* sessionData, const json& errorData);
    json resolvePipelineReferences(RpcSessionData* sessionData, const json& value);
};

// Helper function to set up RPC endpoint with uWebSockets (both WebSocket and HTTP POST)
template<typename App>
void setupRpcEndpoint(App& app, const std::string& path, std::shared_ptr<RpcTarget> target)
{
    auto session = std::make_shared<RpcSession>(target);

    // Set up WebSocket endpoint
    app.template ws<RpcSessionData>(path,
    {
        .open = [session, target](auto* ws)
        {
            auto* userData = ws->getUserData();
            userData->target = target;
            session->onOpen(userData);
        },
        .message = [session](auto* ws, std::string_view message, uWS::OpCode opCode)
        {
            auto* userData = ws->getUserData();
            try
            {
                #if 0
                std::cout << __FUNCTION__ << ": received message: " << message << std::endl;
                #endif
                std::string response = session->handleMessage(userData, std::string(message));
                if (!response.empty())
                    ws->send(response, uWS::TEXT);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error processing message: " << e.what() << std::endl;
            }
        },
        .drain = [](auto* ws)
        {
            // Called when ws is able to send more data
        },
        .close = [session](auto* ws, int code, std::string_view message)
        {
            auto* userData = ws->getUserData();
            session->onClose(userData);
        }
    });

    // Set up HTTP POST endpoint for batch RPC using the same protocol
    app.post(path, [session, target](auto* res, auto* req)
    {
        std::string body;

        res->onAborted([]() {
            std::cerr << "HTTP request aborted" << std::endl;
        });

        res->onData([res, session, target, body = std::move(body)](std::string_view data, bool isEnd) mutable
        {
            body.append(data);

            if (isEnd)
            {
                try
                {
                    #if 0
                    std::cout << "HTTP POST received: " << body << std::endl;
                    #endif

                    // Create a session data for this HTTP batch request
                    RpcSessionData sessionData;
                    sessionData.target = target;
                    sessionData.nextExportId = 1;

                    // Split the body by newlines to get individual messages
                    std::vector<std::string> responses;
                    std::istringstream stream(body);
                    std::string line;

                    while (std::getline(stream, line))
                    {
                        if (!line.empty())
                        {
                            // Process each message line
                            std::string response = session->handleMessage(&sessionData, line);
                            if (!response.empty())
                            {
                                responses.push_back(response);
                            }
                        }
                    }

                    res->writeHeader("Content-Type", "text/plain");
                    res->writeHeader("Access-Control-Allow-Origin", "*");

                    // Join responses with newlines
                    std::string responseBody;
                    for (size_t i = 0; i < responses.size(); ++i)
                    {
                        if (i > 0) responseBody += "\n";
                        responseBody += responses[i];
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

    // Also set up OPTIONS for CORS preflight
    app.options(path, [](auto* res, auto* req)
    {
        res->writeHeader("Access-Control-Allow-Origin", "*");
        res->writeHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
        res->writeHeader("Access-Control-Allow-Headers", "Content-Type");
        res->end();
    });
}

} // namespace capnwebcpp
