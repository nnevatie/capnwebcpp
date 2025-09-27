#include "capnwebcpp/rpc_service.hpp"

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

            // Store the operation for lazy evaluation when pulled
            sessionData->pendingOperations[exportId] = json::object({
                {"method", method},
                {"args", argsArray}
            });
        }
    }
}

json RpcSession::resolvePipelineReferences(RpcSessionData* sessionData, const json& value)
{
    if (value.is_array())
    {
        // Check if this is a pipeline reference: ["pipeline", exportId, ["path", ...]]
        if (value.size() >= 2 && value[0].is_string() && value[0] == "pipeline" && value[1].is_number())
        {
            int refExportId = value[1];

            // First check if the result is already computed
            auto it = sessionData->pendingResults.find(refExportId);
            if (it != sessionData->pendingResults.end())
            {
                json result = it->second;

                // If there's a path, traverse it
                if (value.size() >= 3 && value[2].is_array())
                {
                    for (const auto& key : value[2])
                    {
                        if (key.is_string() && result.is_object())
                        {
                            result = result[key.get<std::string>()];
                        }
                        else if (key.is_number() && result.is_array())
                        {
                            result = result[key.get<int>()];
                        }
                    }
                }

                return result;
            }
            // If not cached, check if we need to execute a pending operation
            else if (sessionData->pendingOperations.find(refExportId) != sessionData->pendingOperations.end())
            {
                json& operation = sessionData->pendingOperations[refExportId];
                std::string method = operation["method"];
                json args = operation["args"];

                // Recursively resolve arguments (in case they have pipeline references too)
                json resolvedArgs = resolvePipelineReferences(sessionData, args);

                // Execute the operation
                json result = sessionData->target->dispatch(method, resolvedArgs);

                // Cache the result
                sessionData->pendingResults[refExportId] = result;
                sessionData->pendingOperations.erase(refExportId);

                // If there's a path, traverse it
                if (value.size() >= 3 && value[2].is_array())
                {
                    for (const auto& key : value[2])
                    {
                        if (key.is_string() && result.is_object())
                        {
                            result = result[key.get<std::string>()];
                        }
                        else if (key.is_number() && result.is_array())
                        {
                            result = result[key.get<int>()];
                        }
                    }
                }

                return result;
            }
            else
            {
                throw std::runtime_error("Pipeline reference to non-existent export: " + std::to_string(refExportId));
            }
        }
        else
        {
            // Not a pipeline reference, but still an array - recursively resolve elements
            json resolved = json::array();
            for (const auto& elem : value)
            {
                resolved.push_back(resolvePipelineReferences(sessionData, elem));
            }
            return resolved;
        }
    }
    else if (value.is_object())
    {
        // Recursively resolve object values
        json resolved = json::object();
        for (auto& [key, val] : value.items())
        {
            resolved[key] = resolvePipelineReferences(sessionData, val);
        }
        return resolved;
    }
    else
    {
        // Primitive value, return as-is
        return value;
    }
}

json RpcSession::handlePull(RpcSessionData* sessionData, int exportId)
{
    // Check if we already have a cached result
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
        std::cout << __FUNCTION__ << ": sending response: " << response.dump() << std::endl;
        return response;
    }
    // Check if we have a pending operation to execute
    else if (sessionData->pendingOperations.find(exportId) != sessionData->pendingOperations.end())
    {
        json& operation = sessionData->pendingOperations[exportId];
        std::string method = operation["method"];
        json args = operation["args"];

        try
        {
            // Resolve any pipeline references in the arguments
            json resolvedArgs = resolvePipelineReferences(sessionData, args);

            // Dispatch the method call to the target
            json result = sessionData->target->dispatch(method, resolvedArgs);

            // Store the result for future reference
            sessionData->pendingResults[exportId] = result;

            // Clean up the operation
            sessionData->pendingOperations.erase(exportId);

            // Send as resolve
            json response = json::array({"resolve", exportId, result});
            std::cout << __FUNCTION__ << ": sending response: " << response.dump() << std::endl;
            return response;
        }
        catch (const std::exception& e)
        {
            // Clean up the operation
            sessionData->pendingOperations.erase(exportId);

            // Send as reject
            json error = json::array({"error", "MethodError", std::string(e.what())});
            json response = json::array({"reject", exportId, error});
            std::cout << __FUNCTION__ << ": sending response: " << response.dump() << std::endl;
            return response;
        }
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
