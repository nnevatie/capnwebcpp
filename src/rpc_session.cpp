#include "capnwebcpp/rpc_session.h"
#include "capnwebcpp/protocol.h"
#include "capnwebcpp/serialize.h"

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
    protocol::Message m;
    if (!protocol::parse(message, m))
        return "";

    switch (m.type)
    {
        case protocol::MessageType::Push:
        {
            if (m.params.size() >= 1)
                handlePush(sessionData, m.params[0]);
            return "";
        }
        case protocol::MessageType::Pull:
        {
            if (m.params.size() >= 1 && m.params[0].is_number())
            {
                auto out = handlePull(sessionData, m.params[0]);
                return protocol::serialize(out);
            }
            return "";
        }
        case protocol::MessageType::Release:
        {
            if (m.params.size() >= 2 && m.params[0].is_number() && m.params[1].is_number())
                handleRelease(sessionData, m.params[0], m.params[1]);
            return "";
        }
        case protocol::MessageType::Abort:
        {
            if (m.params.size() >= 1)
                handleAbort(sessionData, m.params[0]);
            return "";
        }
        default:
            return "";
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

protocol::Message RpcSession::handlePull(RpcSessionData* sessionData, int exportId)
{
    if (sessionData->pendingResults.find(exportId) != sessionData->pendingResults.end())
    {
        json& result = sessionData->pendingResults[exportId];
        protocol::Message msg;
        if (result.is_array() && result.size() >= 2 && result[0] == "error")
        {
            msg.type = protocol::MessageType::Reject;
            msg.params = json::array({ exportId, result });
        }
        else
        {
            msg.type = protocol::MessageType::Resolve;
            msg.params = json::array({ exportId, serialize::wrapArrayIfNeeded(result) });
        }
        sessionData->pendingResults.erase(exportId);
        return msg;
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

            protocol::Message msg;
            msg.type = protocol::MessageType::Resolve;
            msg.params = json::array({ exportId, serialize::wrapArrayIfNeeded(result) });
            return msg;
        }
        catch (const std::exception& e)
        {
            sessionData->pendingOperations.erase(exportId);

            protocol::Message msg;
            msg.type = protocol::MessageType::Reject;
            msg.params = json::array({ exportId, serialize::makeError("MethodError", std::string(e.what())) });
            return msg;
        }
    }
    else
    {
        protocol::Message msg;
        msg.type = protocol::MessageType::Reject;
        msg.params = json::array({ exportId, serialize::makeError("ExportNotFound", "Export ID not found") });
        return msg;
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
