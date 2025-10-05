#include "capnwebcpp/rpc_session.h"

#include <iostream>
#include <sstream>

namespace capnwebcpp
{

void RpcSession::onOpen(RpcSessionData* sessionData)
{
    std::cout << "WebSocket connection opened" << std::endl;
    sessionData->nextExportId = 1;
    sessionData->pendingResults.clear();
}

void RpcSession::onClose(RpcSessionData*)
{
    std::cout << "WebSocket connection closed" << std::endl;
}

std::string RpcSession::handleMessage(RpcSessionData* sessionData, const std::string& message)
{
    try
    {
        auto msg = json::parse(message);

        if (!msg.is_array() || msg.empty())
            return "";

        std::string messageType = msg[0];

        if (messageType == "push")
        {
            handlePush(sessionData, msg[1]);
            return "";
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
                return "";
            }
        }
        else if (messageType == "abort")
        {
            handleAbort(sessionData, msg[1]);
            return "";
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

    int exportId = sessionData->nextExportId++;

    if (pushData[0] == "pipeline" && pushData.size() >= 3)
    {
        int importId = pushData[1];
        (void)importId;
        auto methodArray = pushData[2];
        auto argsArray = pushData.size() >= 4 ? pushData[3] : json::array();

        if (methodArray.is_array() && !methodArray.empty())
        {
            std::string method = methodArray[0];

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
        if (value.size() >= 2 && value[0].is_string() && value[0] == "pipeline" && value[1].is_number())
        {
            int refExportId = value[1];

            auto it = sessionData->pendingResults.find(refExportId);
            if (it != sessionData->pendingResults.end())
            {
                json result = it->second;

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
            else if (sessionData->pendingOperations.find(refExportId) != sessionData->pendingOperations.end())
            {
                json& operation = sessionData->pendingOperations[refExportId];
                std::string method = operation["method"];
                json args = operation["args"];

                json resolvedArgs = resolvePipelineReferences(sessionData, args);

                json result = sessionData->target->dispatch(method, resolvedArgs);

                sessionData->pendingResults[refExportId] = result;
                sessionData->pendingOperations.erase(refExportId);

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
            json resolved = json::array();
            for (const auto& elem : value)
                resolved.push_back(resolvePipelineReferences(sessionData, elem));
            return resolved;
        }
    }
    else if (value.is_object())
    {
        json resolved = json::object();
        for (auto& [key, val] : value.items())
            resolved[key] = resolvePipelineReferences(sessionData, val);
        return resolved;
    }
    else
    {
        return value;
    }
}

json RpcSession::handlePull(RpcSessionData* sessionData, int exportId)
{
    if (sessionData->pendingResults.find(exportId) != sessionData->pendingResults.end())
    {
        json& result = sessionData->pendingResults[exportId];
        json response;

        if (result.is_array() && result.size() >= 2 && result[0] == "error")
        {
            response = json::array({"reject", exportId, result});
        }
        else
        {
            if (result.is_array())
                response = json::array({"resolve", exportId, json::array({result})});
            else
                response = json::array({"resolve", exportId, result});
        }

        sessionData->pendingResults.erase(exportId);
        return response;
    }
    else if (sessionData->pendingOperations.find(exportId) != sessionData->pendingOperations.end())
    {
        json& operation = sessionData->pendingOperations[exportId];
        std::string method = operation["method"];
        json args = operation["args"];

        try
        {
            json resolvedArgs = resolvePipelineReferences(sessionData, args);

            json result = sessionData->target->dispatch(method, resolvedArgs);

            sessionData->pendingResults[exportId] = result;
            sessionData->pendingOperations.erase(exportId);

            json response;
            if (result.is_array())
                response = json::array({"resolve", exportId, json::array({result})});
            else
                response = json::array({"resolve", exportId, result});

            return response;
        }
        catch (const std::exception& e)
        {
            sessionData->pendingOperations.erase(exportId);

            json error = json::array({"error", "MethodError", std::string(e.what())});
            json response = json::array({"reject", exportId, error});
            return response;
        }
    }
    else
    {
        return json::array({"reject", exportId, json::array({
            "error", "ExportNotFound", "Export ID not found"
        })});
    }
}

void RpcSession::handleRelease(RpcSessionData*, int exportId, int refcount)
{
    std::cout << "Released export " << exportId << " with refcount " << refcount << std::endl;
}

void RpcSession::handleAbort(RpcSessionData*, const json& errorData)
{
    std::cerr << "Abort received: " << errorData.dump() << std::endl;
}

} // namespace capnwebcpp
