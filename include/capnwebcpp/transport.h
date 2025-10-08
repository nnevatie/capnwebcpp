#pragma once

#include <string>

#include "capnwebcpp/rpc_session.h"
#include "capnwebcpp/protocol.h"

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
    int pullExportId = 0;
    {
        capnwebcpp::protocol::Message m;
        if (capnwebcpp::protocol::parse(message, m))
        {
            if (m.type == capnwebcpp::protocol::MessageType::Pull && m.params.size() >= 1 && m.params[0].is_number())
            {
                pullExportId = m.params[0];
            }
        }
    }

    std::string response = session.handleMessage(sessionData, message);
    if (!response.empty())
    {
        transport.send(response);
    }
    if (session.isAborted())
    {
        // Best-effort attempt to close the transport after abort.
        transport.abort("aborted");
    }

    if (pullExportId != 0 && sessionData)
    {
        if (auto* e = sessionData->exporter.find(pullExportId))
        {
            for (const auto& kv : e->importedClientIds)
            {
                int importId = kv.first;
                int count = kv.second;
                if (count <= 0) continue;
                capnwebcpp::protocol::Message rel;
                rel.type = capnwebcpp::protocol::MessageType::Release;
                rel.params = json::array({ importId, count });
                transport.send(capnwebcpp::protocol::serialize(rel));
            }
            e->importedClientIds.clear();
        }
    }
}

} // namespace capnwebcpp
