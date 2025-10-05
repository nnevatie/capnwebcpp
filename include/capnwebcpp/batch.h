#pragma once

#include <string>
#include <vector>
#include <sstream>

#include "capnwebcpp/rpc_session.h"

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
            std::string response = session.handleMessage(sessionData, line);
            if (!response.empty())
                responses.push_back(response);
        }
    }
    return responses;
}

} // namespace capnwebcpp

