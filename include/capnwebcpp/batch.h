#pragma once

#include <string>
#include <vector>
#include <sstream>

#include "capnwebcpp/rpc_session.h"
#include "capnwebcpp/logging.h"

namespace capnwebcpp
{

// Process a newline-delimited batch body and return newline-delimited responses.
inline std::vector<std::string> processBatch(RpcSession& session, RpcSessionData* sessionData, const std::string& body)
{
    std::vector<std::string> responses;
    std::istringstream stream(body);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty())
        {
            debugLog(std::string("batch line: ") + line);
            std::string response = session.handleMessage(sessionData, line);
            // After each message, run microtasks (simulate microtask queue).
            session.processTasks();
            if (!response.empty())
            {
                debugLog(std::string("batch response: ") + response);
                responses.push_back(response);
            }
        }
    }
    debugLog(std::string("batch done, responses=") + std::to_string(responses.size()));
    return responses;
}

} // namespace capnwebcpp
