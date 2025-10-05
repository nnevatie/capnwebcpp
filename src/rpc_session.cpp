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
    pullCount = 0;
    aborted = false;
}

void RpcSession::onClose(RpcSessionData*)
{
    std::cout << "WebSocket connection closed" << std::endl;
}

std::string RpcSession::handleMessage(RpcSessionData* sessionData, const std::string& message)
{
    if (aborted)
        return "";
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
                ++pullCount;
                auto out = handlePull(sessionData, m.params[0]);
                if (pullCount > 0) --pullCount;
                return protocol::serialize(out);
            }
            return "";
        }
        case protocol::MessageType::Resolve:
        case protocol::MessageType::Reject:
        {
            // [type, importId, valueOrError]
            if (m.params.size() >= 2 && m.params[0].is_number())
            {
                int importId = m.params[0];
                auto& imp = sessionData->imports[importId];
                imp.hasResolution = true;
                imp.resolution = m.params[1];

                // Parity: after import resolves/rejects, send release for remote refs.
                protocol::Message rel;
                rel.type = protocol::MessageType::Release;
                int releaseCount = imp.remoteRefcount > 0 ? imp.remoteRefcount : 1;
                rel.params = json::array({ importId, releaseCount });
                // Clean up local import entry.
                sessionData->imports.erase(importId);
                return protocol::serialize(rel);
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
        auto methodArray = pushData[2];
        auto argsArray = pushData.size() >= 4 ? pushData[3] : json::array();

        if (methodArray.is_array() && !methodArray.empty())
        {
            std::string method = methodArray[0];

            // Determine the target to dispatch the call on.
            std::shared_ptr<RpcTarget> callTarget = sessionData->target;
            auto itTarget = sessionData->exports.find(importId);
            if (importId != 0 && itTarget != sessionData->exports.end() && itTarget->second.callTarget)
            {
                callTarget = itTarget->second.callTarget;
            }

            ExportEntry entry;
            entry.remoteRefcount = 1;
            entry.hasOperation = true;
            entry.method = method;
            entry.args = argsArray;
            entry.callTarget = callTarget;

            // Defer evaluation to microtask queue; transmit still waits for pull.
            int queuedExportId = exportId;
            json queuedArgs = argsArray;
            enqueueTask([this, sessionData, queuedExportId, method, queuedArgs, callTarget]() mutable
            {
                auto it = sessionData->exports.find(queuedExportId);
                if (it == sessionData->exports.end()) return;
                try
                {
                    json resolvedArgs = resolvePipelineReferences(sessionData, queuedArgs);
                    json result = callTarget->dispatch(method, resolvedArgs);
                    it->second.hasOperation = false;
                    it->second.hasResult = true;
                    it->second.result = result;
                }
                catch (const std::exception& e)
                {
                    it->second.hasOperation = false;
                    it->second.hasResult = true;
                    it->second.result = serialize::makeError("MethodError", std::string(e.what()));
                }
            });

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
    // Before responding, process any queued microtasks so results are ready.
    processTasks();
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
            // Devaluate result for exports/promises; then wrap arrays unless special expression.
            json deval = serialize::devaluateForResult(result, [this, sessionData](bool isPromise, const json& payload) {
                int id = allocateNegativeExportId(sessionData);
                ExportEntry e; e.remoteRefcount = 1; e.hasOperation = false;
                if (isPromise)
                {
                    e.hasResult = true;
                    e.result = payload; // Promise resolves to this payload when pulled
                }
                else
                {
                    e.hasResult = false;
                }
                e.callTarget = sessionData->target;
                sessionData->exports[id] = e;
                return id;
            });
            if (deval.is_array() && serialize::isSpecialArray(deval))
                msg.params = json::array({ exportId, deval });
            else
                msg.params = json::array({ exportId, serialize::wrapArrayIfNeeded(deval) });
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

            std::shared_ptr<RpcTarget> callTarget = itExp->second.callTarget ? itExp->second.callTarget : sessionData->target;
            json result = callTarget->dispatch(method, resolvedArgs);
            itExp->second.hasOperation = false;
            itExp->second.method.clear();
            itExp->second.args = json();
            itExp->second.hasResult = true;
            itExp->second.result = result;

            protocol::Message msg;
            msg.type = protocol::MessageType::Resolve;
            json deval = serialize::devaluateForResult(result, [this, sessionData](bool isPromise, const json& payload) {
                int id = allocateNegativeExportId(sessionData);
                ExportEntry e; e.remoteRefcount = 1; e.hasOperation = false;
                if (isPromise)
                {
                    e.hasResult = true;
                    e.result = payload;
                }
                else
                {
                    e.hasResult = false;
                }
                e.callTarget = sessionData->target;
                sessionData->exports[id] = e;
                return id;
            });
            if (deval.is_array() && serialize::isSpecialArray(deval))
                msg.params = json::array({ exportId, deval });
            else
                msg.params = json::array({ exportId, serialize::wrapArrayIfNeeded(deval) });
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
            json err = serialize::makeError("MethodError", std::string(e.what()));
            if (onSendError)
                err = onSendError(err);
            msg.params = json::array({ exportId, err });
            return msg;
        }
    }
    else
    {
        protocol::Message msg;
        msg.type = protocol::MessageType::Reject;
        {
            json err = serialize::makeError("ExportNotFound", "Export ID not found");
            if (onSendError)
                err = onSendError(err);
            msg.params = json::array({ exportId, err });
        }
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
        it->second.remoteRefcount -= refcount;
    }

    if (it->second.remoteRefcount <= 0)
    {
        sessionData->exports.erase(it);
    }
}

void RpcSession::handleAbort(RpcSessionData*, const json& errorData)
{
    std::cerr << "Abort received: " << errorData.dump() << std::endl;
    aborted = true;
    std::string reason = errorData.is_string() ? errorData.get<std::string>() : errorData.dump();
    for (auto& cb : onBrokenCallbacks)
    {
        try { cb(reason); } catch (...) {}
    }
}

} // namespace capnwebcpp
