#pragma once

#include <string>

#include "capnwebcpp/rpc_session.h"

namespace capnwebcpp
{

// Abstract transport interface. Implementations deliver strings to the peer.
class RpcTransport
{
public:
    virtual ~RpcTransport() = default;
    virtual void send(const std::string& message) = 0;
    virtual void abort(const std::string& reason) = 0;
};

// Helper to process a single inbound message via session and send any response.
inline void pumpMessage(RpcSession& session,
                        RpcSessionData* sessionData,
                        RpcTransport& transport,
                        const std::string& message)
{
    std::string response = session.handleMessage(sessionData, message);
    if (!response.empty())
    {
        transport.send(response);
    }
}

} // namespace capnwebcpp

