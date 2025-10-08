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
    sessionData->reverseExport.clear();
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
    json payload = errorPayload;
    // Apply error redaction if configured and payload is an error tuple.
    if (onSendError && payload.is_array() && payload.size() >= 3 && payload[0] == "error")
    {
        try { payload = onSendError(payload); } catch (...) {}
    }
    msg.params = json::array({ payload });
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

void RpcSession::markAborted(RpcSessionData* sessionData, const std::string& reason)
{
    // Mark aborted and notify listeners.
    markAborted(reason);
    // Clear microtasks and reset counters.
    microtasks.clear();
    pendingMicrotasks = 0;
    // Best-effort cleanup of session tables and maps.
    if (sessionData)
    {
        sessionData->exporter.reset();
        sessionData->importer.reset();
        sessionData->reverseExport.clear();
        sessionData->importToPromiseExport.clear();
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

            // Scan arguments for client-imported refs to release after completion.
            auto* entry = sessionData->exporter.find(exportId);
            if (entry)
            {
                std::function<void(const json&)> scan = [&](const json& v)
                {
                    if (v.is_array())
                    {
                        if (!v.empty() && v[0].is_string() && v.size() >= 2 && v[1].is_number_integer())
                        {
                            std::string tag = v[0];
                            if (tag == "export" || tag == "promise")
                            {
                                int id = v[1].get<int>();
                                entry->importedClientIds[id] += 1;
                                return;
                            }
                        }
                        for (const auto& e : v) scan(e);
                    }
                    else if (v.is_object())
                    {
                        for (auto it = v.begin(); it != v.end(); ++it) scan(it.value());
                    }
                };
                scan(argsArray);
            }

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

                // Build pipeline inner array; omit args when performing a property get.
                json inner = json::array({ "pipeline", exportId, path });
                if (!args.is_null())
                {
                    inner.push_back(args);
                }
                json pushExpr = json::array({ "push", inner });
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

            // Record captured client export refs for release.
            if (pushData.size() >= 4 && pushData[3].is_array())
            {
                const json& captures = pushData[3];
                for (const auto& cap : captures)
                {
                    if (cap.is_array() && cap.size() == 2 && cap[0].is_string() && cap[1].is_number_integer())
                    {
                        std::string tag = cap[0];
                        if (tag == "export")
                        {
                            int id = cap[1].get<int>();
                            entry.importedClientIds[id] += 1;
                        }
                    }
                }
            }

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

int RpcSession::callClient(RpcSessionData* sessionData, int exportId, const json& path, const json& args)
{
    if (!sessionData || !sessionData->transport)
        throw std::runtime_error("server-to-client call requires a persistent transport");

    // Allocate import ID for the call result.
    int callImportId = sessionData->importer.allocatePositiveImportId();

    // Build pipeline; omit args if null/empty to produce a property get.
    json inner = json::array({ "pipeline", exportId, path });
    if (!args.is_null() && !(args.is_array() && args.empty()))
    {
        inner.push_back(args);
    }
    json pushExpr = json::array({ "push", inner });
    sessionData->transport->send(pushExpr.dump());

    // Trigger pull so the peer will send resolve/reject for this import.
    json pullExpr = json::array({ "pull", callImportId });
    sessionData->transport->send(pullExpr.dump());

    // Create promise export for the peer; link import -> promise for forwarding.
    int promiseExportId = allocateNegativeExportId(sessionData);
    ExportEntry e; e.remoteRefcount = 1; e.hasOperation = false; e.hasResult = false;
    sessionData->exporter.put(promiseExportId, e);
    sessionData->importToPromiseExport[callImportId] = promiseExportId;
    return promiseExportId;
}

int RpcSession::callClientMethod(RpcSessionData* sessionData, int exportId, const std::string& method, const json& argsArray)
{
    json path = json::array({ method });
    return callClient(sessionData, exportId, path, argsArray);
}

int RpcSession::awaitClientPromise(RpcSessionData* sessionData, int importId)
{
    if (!sessionData)
        throw std::runtime_error("awaitClientPromise: sessionData is null");
    int promiseExportId = allocateNegativeExportId(sessionData);
    ExportEntry e; e.remoteRefcount = 1; e.hasOperation = false; e.hasResult = false;
    sessionData->exporter.put(promiseExportId, e);
    sessionData->importToPromiseExport[importId] = promiseExportId;
    return promiseExportId;
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
            json err = result;
            if (onSendError)
            {
                try { err = onSendError(err); } catch (...) {}
            }
            msg.params = json::array({ exportId, err });
        }
        else
        {
            msg.type = protocol::MessageType::Resolve;
            // Devaluate result for exports/promises; then wrap arrays unless special expression.
            json deval = serialize::devaluateForResult(result, [this, sessionData](bool isPromise, const json& payload) {
                if (!isPromise)
                {
                    // Re-export parity: reuse existing export ID for the canonical local target hook.
                    auto hook = sessionData->localTargetHook ? sessionData->localTargetHook
                                                             : makeLocalTargetHook(sessionData->target);
                    if (!sessionData->localTargetHook) sessionData->localTargetHook = hook;
                    std::uintptr_t key = reinterpret_cast<std::uintptr_t>(hook.get());
                    auto it = sessionData->reverseExport.find(key);
                    if (it != sessionData->reverseExport.end())
                    {
                        int existingId = it->second;
                        if (auto* entry = sessionData->exporter.find(existingId))
                        {
                            entry->remoteRefcount += 1;
                        }
                        return existingId;
                    }
                    int id = allocateNegativeExportId(sessionData);
                    ExportEntry e; e.remoteRefcount = 1; e.hasOperation = false; e.hasResult = false;
                    e.callHook = hook;
                    sessionData->exporter.put(id, e);
                    sessionData->reverseExport[key] = id;
                    return id;
                }
                else
                {
                    int id = allocateNegativeExportId(sessionData);
                    ExportEntry e; e.remoteRefcount = 1; e.hasOperation = false;
                    e.hasResult = true;
                    e.result = payload; // Promise resolves to this payload when pulled
                    e.callHook = sessionData->localTargetHook ? sessionData->localTargetHook
                                                              : makeLocalTargetHook(sessionData->target);
                    if (!sessionData->localTargetHook) sessionData->localTargetHook = e.callHook;
                    sessionData->exporter.put(id, e);
                    return id;
                }
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
                if (!isPromise)
                {
                    auto hook = sessionData->localTargetHook ? sessionData->localTargetHook
                                                             : makeLocalTargetHook(sessionData->target);
                    if (!sessionData->localTargetHook) sessionData->localTargetHook = hook;
                    std::uintptr_t key = reinterpret_cast<std::uintptr_t>(hook.get());
                    auto it = sessionData->reverseExport.find(key);
                    if (it != sessionData->reverseExport.end())
                    {
                        int existingId = it->second;
                        if (auto* entry = sessionData->exporter.find(existingId))
                        {
                            entry->remoteRefcount += 1;
                        }
                        return existingId;
                    }
                    int id = allocateNegativeExportId(sessionData);
                    ExportEntry e; e.remoteRefcount = 1; e.hasOperation = false; e.hasResult = false;
                    e.callHook = hook;
                    sessionData->exporter.put(id, e);
                    sessionData->reverseExport[key] = id;
                    return id;
                }
                else
                {
                    int id = allocateNegativeExportId(sessionData);
                    ExportEntry e; e.remoteRefcount = 1; e.hasOperation = false;
                    e.hasResult = true;
                    e.result = payload;
                    e.callHook = sessionData->localTargetHook ? sessionData->localTargetHook
                                                              : makeLocalTargetHook(sessionData->target);
                    if (!sessionData->localTargetHook) sessionData->localTargetHook = e.callHook;
                    sessionData->exporter.put(id, e);
                    return id;
                }
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

void RpcSession::handleAbort(RpcSessionData* sessionData, const json& errorData)
{
    std::cerr << "Abort received: " << errorData.dump() << std::endl;
    std::string reason = errorData.is_string() ? errorData.get<std::string>() : errorData.dump();
    markAborted(sessionData, reason);
}

} // namespace capnwebcpp
