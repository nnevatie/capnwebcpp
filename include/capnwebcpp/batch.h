#pragma once

#include <string>
#include <vector>
#include <sstream>

#include "capnwebcpp/rpc_session.h"
#include "capnwebcpp/logging.h"
#include "capnwebcpp/transport.h"
#include "capnwebcpp/transports/accum_transport.h"

namespace capnwebcpp
{

// Process a newline-delimited batch body using an accumulating transport.
// Returns all outbound messages (responses and any server->client frames) in send order.
inline std::vector<std::string> processBatch(RpcSession& session, RpcSessionData* sessionData, const std::string& body)
{
    std::vector<std::string> outbox;
    auto transportPtr = std::make_shared<AccumTransport>(outbox);
    if (sessionData) sessionData->transport = transportPtr;

    std::istringstream stream(body);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty())
        {
            debugLog(std::string("batch line: ") + line);
            pumpMessage(session, sessionData, *transportPtr, line);
            // After each message, run microtasks (simulate microtask queue).
            session.processTasks();
        }
    }
    // Drain any remaining queued tasks before returning accumulated messages.
    session.drain(sessionData);
    debugLog(std::string("batch done, outbox=") + std::to_string(outbox.size()));
    return outbox;
}

} // namespace capnwebcpp
