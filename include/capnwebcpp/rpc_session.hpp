#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <memory>
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
        auto it = methods_.find(method);
        if (it != methods_.end())
            return it->second(args);

        throw std::runtime_error("Method not found: " + method);
    }

protected:
    // Helper to register methods
    void method(const std::string& name, std::function<json(const json&)> handler)
    {
        methods_[name] = handler;
    }

private:
    std::unordered_map<std::string, std::function<json(const json&)>> methods_;
};

// Internal data associated with each WebSocket connection
struct RpcSessionData
{
    std::unordered_map<int, json> pendingResults;
    int nextExportId = 1;
    std::shared_ptr<RpcTarget> target;
};

// RpcSession handles the Cap'n Web RPC protocol for a WebSocket connection
class RpcSession
{
public:
    RpcSession(std::shared_ptr<RpcTarget> target) : target_(target) {}

    // Handle incoming WebSocket message
    std::string handleMessage(RpcSessionData* sessionData, const std::string& message);

    // Called when a new WebSocket connection is opened
    void onOpen(RpcSessionData* sessionData);

    // Called when a WebSocket connection is closed
    void onClose(RpcSessionData* sessionData);

private:
    std::shared_ptr<RpcTarget> target_;

    void handlePush(RpcSessionData* sessionData, const json& pushData);
    json handlePull(RpcSessionData* sessionData, int exportId);
    void handleRelease(RpcSessionData* sessionData, int exportId, int refcount);
    void handleAbort(RpcSessionData* sessionData, const json& errorData);
};

// Helper function to set up WebSocket RPC endpoint with uWebSockets
template<typename App>
void setupRpcEndpoint(App& app, const std::string& path, std::shared_ptr<RpcTarget> target)
{
    auto session = std::make_shared<RpcSession>(target);

    app.template ws<RpcSessionData>(path, {
        .compression = uWS::DISABLED,
        .maxPayloadLength = 16 * 1024 * 1024,
        .idleTimeout = 120,
        .maxBackpressure = 1 * 1024 * 1024,
        .closeOnBackpressureLimit = false,
        .resetIdleTimeoutOnSend = false,
        .sendPingsAutomatically = true,

        .upgrade = nullptr,
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
}

} // namespace capnwebcpp
