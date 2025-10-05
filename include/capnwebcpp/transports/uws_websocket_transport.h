#pragma once

#include <string>
#include <App.h>

#include "capnwebcpp/transport.h"

namespace capnwebcpp
{

// RpcTransport adapter for uWebSockets WebSocket instances.
template<typename WebSocketPtr>
class UwsWebSocketTransport : public RpcTransport
{
public:
    explicit UwsWebSocketTransport(WebSocketPtr socket) : socket(socket) {}

    void send(const std::string& message) override
    {
        socket->send(message, uWS::TEXT);
    }

    void abort(const std::string& /*reason*/) override
    {
        // No-op for now. Endpoint controls socket lifecycle.
    }

private:
    WebSocketPtr socket;
};

} // namespace capnwebcpp

