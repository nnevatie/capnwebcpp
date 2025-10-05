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
    sessionData->exports.clear();
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
        case protocol::MessageType::Resolve:
        {
            // ["resolve", importId, value]
            if (m.params.size() >= 2 && m.params[0].is_number())
            {
                int importId = m.params[0];
                auto& imp = sessionData->imports[importId];
                imp.hasResolution = true;
                imp.resolution = m.params[1];
            }
            return "";
        }
        case protocol::MessageType::Reject:
        {
            // ["reject", importId, error]
            if (m.params.size() >= 2 && m.params[0].is_number())
            {
                int importId = m.params[0];
                auto& imp = sessionData->imports[importId];
                imp.hasResolution = true;
                imp.resolution = m.params[1];
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
            ExportEntry entry;
            entry.refcount = 1;
            entry.hasOperation = true;
            entry.method = method;
            entry.args = argsArray;
            sessionData->exports[exportId] = std::move(entry);
        }
    }
}

json RpcSession::resolvePipelineReferences(RpcSessionData* sessionData, const json& value)
{
    auto getResult = [sessionData](int exportId, json& out) -> bool
    {
        auto it = sessionData->exports.find(exportId);
        if (it != sessionData->exports.end() && it->second.hasResult)
        {
            out = it->second.result;
            return true;
        }
        return false;
    };

    auto getOperation = [sessionData](int exportId, std::string& method, json& args) -> bool
    {
        auto it = sessionData->exports.find(exportId);
        if (it != sessionData->exports.end() && it->second.hasOperation)
        {
            method = it->second.method;
            args = it->second.args;
            return true;
        }
        return false;
    };

    auto dispatch = [this, sessionData](const std::string& method, const json& args) -> json
    {
        return sessionData->target->dispatch(method, args);
    };

    auto cache = [sessionData](int exportId, const json& result)
    {
        auto& entry = sessionData->exports[exportId];
        entry.hasResult = true;
        entry.result = result;
        entry.hasOperation = false;
        entry.method.clear();
        entry.args = json();
    };

    return serialize::Evaluator::evaluateValue(value, getResult, getOperation, dispatch, cache);
}

protocol::Message RpcSession::handlePull(RpcSessionData* sessionData, int exportId)
{
    auto itExp = sessionData->exports.find(exportId);
    if (itExp != sessionData->exports.end() && itExp->second.hasResult)
    {
        json& result = itExp->second.result;
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
        // Clear result after sending; keep entry for refcount tracking if needed.
        itExp->second.hasResult = false;
        itExp->second.result = json();
        return msg;
    }
    else if (itExp != sessionData->exports.end() && itExp->second.hasOperation)
    {
        std::string method = itExp->second.method;
        json args = itExp->second.args;

        try
        {
            json resolvedArgs = resolvePipelineReferences(sessionData, args);

            json result = sessionData->target->dispatch(method, resolvedArgs);
            itExp->second.hasOperation = false;
            itExp->second.method.clear();
            itExp->second.args = json();
            itExp->second.hasResult = true;
            itExp->second.result = result;

            protocol::Message msg;
            msg.type = protocol::MessageType::Resolve;
            msg.params = json::array({ exportId, serialize::wrapArrayIfNeeded(result) });
            return msg;
        }
        catch (const std::exception& e)
        {
            // Clear operation on error as well.
            itExp->second.hasOperation = false;
            itExp->second.method.clear();
            itExp->second.args = json();
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

void RpcSession::handleRelease(RpcSessionData* sessionData, int exportId, int refcount)
{
    auto it = sessionData->exports.find(exportId);
    if (it == sessionData->exports.end())
    {
        std::cout << "Release for unknown exportId " << exportId << std::endl;
        return;
    }

    // Decrement remote refcount and clean up on zero.
    if (refcount > 0)
    {
        it->second.refcount -= refcount;
    }

    if (it->second.refcount <= 0)
    {
        sessionData->exports.erase(it);
    }
}

void RpcSession::handleAbort(RpcSessionData*, const json& errorData)
{
    std::cerr << "Abort received: " << errorData.dump() << std::endl;
}

} // namespace capnwebcpp
