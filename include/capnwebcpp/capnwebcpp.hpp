#pragma once

// Main header for capnwebcpp library
// Provides Cap'n Web RPC protocol implementation for C++

#include "capnwebcpp/rpc_session.hpp"

namespace capnwebcpp
{

// Convenience function to create and run a simple RPC server
template<typename RpcTargetType>
void runRpcServer(int port, const std::string& path = "/api")
{
    auto target = std::make_shared<RpcTargetType>();

    uWS::App app;

    // Setup HTTP endpoint that returns a simple message
    app.get(path, [path](auto* res, auto* req)
    {
        res->end("Cap'n Web RPC endpoint available at WebSocket path: " + path);
    });

    // Setup WebSocket RPC endpoint
    setupRpcEndpoint(app, path, target);

    // Start listening
    app.listen(port, [port](auto* token)
    {
        if (token)
            std::cout << "Listening on port " << port << std::endl;
        else
            std::cerr << "Failed to listen on port " << port << std::endl;
    }).run();
}

} // namespace capnwebcpp
