#include "capnwebcpp/rpc_session.h"
#include "capnwebcpp/transport.h"
#include "capnwebcpp/protocol.h"
#include "capnwebcpp/serialize.h"
#include "capnwebcpp/logging.h"

#include <iostream>
#include <sstream>

namespace capnwebcpp
{

void RpcSession::onOpen(RpcSessionData* sessionData)
{
    std::cout << "WebSocket connection opened" << std::endl;
    sessionData->exporter.reset();
    sessionData->importer.reset();
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
                auto s = protocol::serialize(out);
                debugLog(std::string("pull response: ") + s);
                return s;
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
                // Parity: after import resolves/rejects, send release for remote refs.
                int releaseCount = sessionData->importer.recordResolutionAndGetReleaseCount(importId, m.params[1]);
                protocol::Message rel;
                rel.type = protocol::MessageType::Release;
                rel.params = json::array({ importId, releaseCount });
                // Forward resolution to linked exported promise, if any.
                auto itLink = sessionData->importToPromiseExport.find(importId);
                if (itLink != sessionData->importToPromiseExport.end())
                {
                    int promiseExportId = itLink->second;
                    sessionData->importToPromiseExport.erase(itLink);
                    if (sessionData->transport)
                    {
                        protocol::Message fwd;
                        fwd.type = (m.type == protocol::MessageType::Resolve)
                            ? protocol::MessageType::Resolve
                            : protocol::MessageType::Reject;
                        fwd.params = json::array({ promiseExportId, m.params[1] });
                        try { sessionData->transport->send(protocol::serialize(fwd)); } catch (...) {}
                    }
                }
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

std::string RpcSession::buildAbort(const json& errorPayload)
{
    protocol::Message msg;
    msg.type = protocol::MessageType::Abort;
    msg.params = json::array({ errorPayload });
    return protocol::serialize(msg);
}

void RpcSession::markAborted(const std::string& reason)
{
    aborted = true;
    for (auto& cb : onBrokenCallbacks)
    {
        try { cb(reason); } catch (...) {}
    }
}

void RpcSession::handlePush(RpcSessionData* sessionData, const json& pushData)
{
    if (!pushData.is_array())
        return;

    int exportId = sessionData->exporter.allocateForPush();

    if (pushData[0] == "pipeline" && pushData.size() >= 3)
    {
        int importId = pushData[1];
        auto methodArray = pushData[2];
        auto argsArray = pushData.size() >= 4 ? pushData[3] : json::array();

        if (methodArray.is_array() && !methodArray.empty())
        {
            std::string method = methodArray[0];

            // Determine the hook to dispatch the call on.
            std::shared_ptr<StubHook> callHook = makeLocalTargetHook(sessionData->target);
            if (importId != 0)
            {
                if (auto* src = sessionData->exporter.find(importId))
                {
                    if (src->callHook) callHook = src->callHook;
                }
            }

            sessionData->exporter.setOperation(exportId, method, argsArray, callHook);

            // Defer evaluation to microtask queue; transmit still waits for pull.
            int queuedExportId = exportId;
            json queuedArgs = argsArray;
            enqueueTask([this, sessionData, queuedExportId, method, queuedArgs, callHook]() mutable
            {
                auto* it = sessionData->exporter.find(queuedExportId);
                if (!it) return;
                try
                {
                    json resolvedArgs = resolvePipelineReferences(sessionData, queuedArgs);
                    json result = callHook->call(method, resolvedArgs);
                    it->hasOperation = false;
                    it->hasResult = true;
                    it->result = result;
                }
                catch (const std::exception& e)
                {
                    it->hasOperation = false;
                    it->hasResult = true;
                    it->result = serialize::makeError("MethodError", std::string(e.what()));
                }
            });
        }
    }
    else if (pushData[0] == "remap")
    {
        // Evaluate remap synchronously for reliability in batch.
        ExportEntry entry;
        entry.remoteRefcount = 1;
        entry.hasOperation = false;
        try
        {
            auto getResult = [sessionData](int id, json& out) -> bool
            {
                json r;
                if (sessionData->exporter.getResult(id, r))
                {
                    out = r;
                    return true;
                }
                return false;
            };
            auto getOperation = [sessionData](int id, std::string& method, json& args) -> bool
            {
                return sessionData->exporter.getOperation(id, method, args);
            };
            auto dispatch = [this, sessionData](const std::string& method, const json& args) -> json
            {
                return sessionData->target->dispatch(method, args);
            };
            auto cache = [sessionData](int id, const json& result)
            {
                sessionData->exporter.cacheResult(id, result);
            };
            auto callExport = [this, sessionData](int exportId, const json& path, const json& args) -> json
            {
                if (!sessionData->transport)
                    throw std::runtime_error("client call path unavailable: no transport");

                // Allocate an import ID for the result of this call.
                int callImportId = sessionData->importer.allocatePositiveImportId();

                // Send push to peer to invoke the captured export.
                json pushExpr = json::array({ "push", json::array({ "pipeline", exportId, path, args }) });
                sessionData->transport->send(pushExpr.dump());

                // Send pull for our newly allocated import ID so the peer will deliver resolution.
                json pullExpr = json::array({ "pull", callImportId });
                sessionData->transport->send(pullExpr.dump());

                // Allocate a negative export ID to represent a promise we export to the peer.
                int promiseExportId = allocateNegativeExportId(sessionData);
                ExportEntry promiseEntry; promiseEntry.remoteRefcount = 1; promiseEntry.hasOperation = false; promiseEntry.hasResult = false;
                sessionData->exporter.put(promiseExportId, promiseEntry);

                // Link the import resolution to this exported promise so we can forward it on arrival.
                sessionData->importToPromiseExport[callImportId] = promiseExportId;

                // Return a promise expression to embed in the evaluated value tree.
                return json::array({ "promise", promiseExportId });
            };

            entry.hasResult = true;
            entry.result = serialize::Evaluator::evaluateValueWithCaller(pushData, getResult, getOperation, dispatch, cache, callExport);
        }
        catch (const std::exception& e)
        {
            entry.hasResult = true;
            entry.result = serialize::makeError("MethodError", std::string(e.what()));
        }
        sessionData->exporter.put(exportId, entry);
    }
}

RpcSession::RpcStats RpcSession::getStats(const RpcSessionData* sessionData) const
{
    RpcStats s;
    if (sessionData)
    {
        s.imports = static_cast<int>(sessionData->importer.table.size());
        s.exports = static_cast<int>(sessionData->exporter.table.size());
    }
    return s;
}

json RpcSession::resolvePipelineReferences(RpcSessionData* sessionData, const json& value)
{
    auto getResult = [sessionData](int exportId, json& out) -> bool
    {
        json r;
        if (sessionData->exporter.getResult(exportId, r))
        {
            out = r;
            return true;
        }
        return false;
    };

    auto getOperation = [sessionData](int exportId, std::string& method, json& args) -> bool
    {
        return sessionData->exporter.getOperation(exportId, method, args);
    };

    auto dispatch = [this, sessionData](const std::string& method, const json& args) -> json
    {
        return sessionData->target->dispatch(method, args);
    };

    auto cache = [sessionData](int exportId, const json& result)
    {
        sessionData->exporter.cacheResult(exportId, result);
    };

    return serialize::Evaluator::evaluateValue(value, getResult, getOperation, dispatch, cache);
}

protocol::Message RpcSession::handlePull(RpcSessionData* sessionData, int exportId)
{
    // Before responding, process any queued microtasks so results are ready.
    processTasks();
    auto* itExp = sessionData->exporter.find(exportId);
    if (itExp && itExp->hasResult)
    {
        json& result = itExp->result;
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
                e.callHook = makeLocalTargetHook(sessionData->target);
                sessionData->exporter.put(id, e);
                return id;
            });
            if (deval.is_array() && serialize::isSpecialArray(deval))
                msg.params = json::array({ exportId, deval });
            else
                msg.params = json::array({ exportId, serialize::wrapArrayIfNeeded(deval) });
        }
        // Clear result after sending; keep entry for refcount tracking if needed.
        itExp->hasResult = false;
        itExp->result = json();
        return msg;
    }
    else if (itExp && itExp->hasOperation)
    {
        std::string method = itExp->method;
        json args = itExp->args;

        try
        {
            json resolvedArgs = resolvePipelineReferences(sessionData, args);

            std::shared_ptr<StubHook> callHook = itExp->callHook ? itExp->callHook : makeLocalTargetHook(sessionData->target);
            json result = callHook->call(method, resolvedArgs);
            itExp->hasOperation = false;
            itExp->method.clear();
            itExp->args = json();
            itExp->hasResult = true;
            itExp->result = result;

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
                e.callHook = makeLocalTargetHook(sessionData->target);
                sessionData->exporter.put(id, e);
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
            itExp->hasOperation = false;
            itExp->method.clear();
            itExp->args = json();
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
    auto* it = sessionData->exporter.find(exportId);
    if (!it)
    {
        std::cout << "Release for unknown exportId " << exportId << std::endl;
        return;
    }
    sessionData->exporter.release(exportId, refcount);
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
