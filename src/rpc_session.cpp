#include "capnwebcpp/rpc_session.hpp"
#include <iostream>

namespace capnwebcpp
{

void RpcSession::onOpen(RpcSessionData* sessionData)
{
    std::cout << "WebSocket connection opened" << std::endl;
    sessionData->nextExportId = 1;
    sessionData->pendingResults.clear();
}

void RpcSession::onClose(RpcSessionData* sessionData)
{
    std::cout << "WebSocket connection closed" << std::endl;
}

std::string RpcSession::handleMessage(RpcSessionData* sessionData, const std::string& message)
{
    try
    {
        auto msg = json::parse(message);
        std::cout << "Received message: " << msg.dump() << std::endl;

        if (!msg.is_array() || msg.empty())
        {
            std::cerr << "Invalid message format" << std::endl;
            return "";
        }

        std::string messageType = msg[0];

        if (messageType == "push")
        {
            handlePush(sessionData, msg[1]);
            return "";  // No response for push
        }
        else if (messageType == "pull")
        {
            if (msg.size() >= 2 && msg[1].is_number())
                return handlePull(sessionData, msg[1]).dump();
        }
        else if (messageType == "release")
        {
            if (msg.size() >= 3 && msg[1].is_number() && msg[2].is_number())
            {
                handleRelease(sessionData, msg[1], msg[2]);
                return "";  // No response for release
            }
        }
        else if (messageType == "abort")
        {
            handleAbort(sessionData, msg[1]);
            return "";  // No response for abort
        }
    }
    catch (const json::exception& e)
    {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
    }

    return "";
}

void RpcSession::handlePush(RpcSessionData* sessionData, const json& pushData)
{
    if (!pushData.is_array())
        return;

    // The push creates a new export on the server side
    int exportId = sessionData->nextExportId++;

    if (pushData[0] == "pipeline" && pushData.size() >= 3)
    {
        int importId = pushData[1];
        auto methodArray = pushData[2];
        auto argsArray = pushData.size() >= 4 ? pushData[3] : json::array();

        if (methodArray.is_array() && !methodArray.empty())
        {
            std::string method = methodArray[0];

            try
            {
                // Dispatch the method call to the target
                json result = sessionData->target->dispatch(method, argsArray);

                // Store the result for when the client pulls it
                sessionData->pendingResults[exportId] = result;
            }
            catch (const std::exception& e)
            {
                // Store error for pull
                sessionData->pendingResults[exportId] = json::array({
                    "error", "MethodError", std::string(e.what())
                });
            }
        }
    }
}

json RpcSession::handlePull(RpcSessionData* sessionData, int exportId)
{
    if (sessionData->pendingResults.find(exportId) != sessionData->pendingResults.end())
    {
        // Check if the stored result is an error
        json& result = sessionData->pendingResults[exportId];
        json response;

        if (result.is_array() && result.size() >= 2 && result[0] == "error")
        {
            // Send as reject
            response = json::array({"reject", exportId, result});
        }
        else
        {
            // Send as resolve
            response = json::array({"resolve", exportId, result});
        }

        // Clean up
        sessionData->pendingResults.erase(exportId);
        return response;
    }
    else
    {
        // Export ID not found - send an error
        return json::array({"reject", exportId, json::array({
            "error", "ExportNotFound", "Export ID not found"
        })});
    }
}

void RpcSession::handleRelease(RpcSessionData* sessionData, int exportId, int refcount)
{
    // For now, just acknowledge the release
    std::cout << "Released export " << exportId << " with refcount " << refcount << std::endl;
}

void RpcSession::handleAbort(RpcSessionData* sessionData, const json& errorData)
{
    std::cerr << "Abort received: " << errorData.dump() << std::endl;
}

} // namespace capnwebcpp
