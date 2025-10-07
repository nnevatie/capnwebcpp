#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>
#include <functional>
#include <deque>

#include "capnwebcpp/rpc_target.h"
#include "capnwebcpp/protocol.h"
#include "capnwebcpp/session_state.h"

namespace capnwebcpp
{

using json = nlohmann::json;

// Importer/Exporter roles and RpcSessionData are defined in session_state.h

// RpcSession handles the Cap'n Web RPC protocol for a connection.
class RpcSession
{
public:
    explicit RpcSession(std::shared_ptr<RpcTarget> target) : target(target) {}
    // Optional: rewrite error tuple before sending (e.g., redaction)
    void setOnSendError(std::function<json(const json&)> cb) { onSendError = std::move(cb); }

    // Handle incoming message; returns a response (possibly empty).
    std::string handleMessage(RpcSessionData* sessionData, const std::string& message);

    // Connection lifecycle hooks.
    void onOpen(RpcSessionData* sessionData);
    void onClose(RpcSessionData* sessionData);

    // Lifecycle: return true if there are no outstanding pulls to resolve.
    bool isDrained() const { return pullCount == 0 && pendingMicrotasks == 0; }
    bool isAborted() const { return aborted; }

    // Internal onBroken registration (reserved for future use).
    void registerOnBroken(std::function<void(const std::string&)> cb) { onBrokenCallbacks.push_back(std::move(cb)); }

    // Build a serialized abort frame with the given error payload.
    // Intended to be sent to the peer prior to closing the connection.
    std::string buildAbort(const json& errorPayload);

    // Mark session aborted locally and notify registered onBroken callbacks.
    void markAborted(const std::string& reason);
    // Overload with access to sessionData for deeper cleanup (tables, queues).
    void markAborted(RpcSessionData* sessionData, const std::string& reason);

    // ----------------------------------------------------------------------------
    // Parity helpers

    // Stats reported to caller for parity with capnweb's getStats().
    struct RpcStats
    {
        int imports = 0;
        int exports = 0;
    };

    // Compute stats (counts of active imports / exports) from session state.
    RpcStats getStats(const RpcSessionData* sessionData) const;

    // Process microtasks until no outstanding work remains.
    void drain(RpcSessionData* /*sessionData*/)
    {
        // For now, drain is equivalent to processing queued tasks until none remain and
        // no pulls are in-flight. In this single-threaded model, processTasks() will
        // empty the microtask queue synchronously.
        while (!isDrained())
        {
            processTasks();
        }
    }

private:
    std::shared_ptr<RpcTarget> target;

    // Lifecycle state
    int pullCount = 0;
    bool aborted = false;
    std::vector<std::function<void(const std::string&)>> onBrokenCallbacks;
    std::function<json(const json&)> onSendError;

    // Microtask queue for deferred operation resolution (simulated async).
    std::deque<std::function<void()>> microtasks;
    int pendingMicrotasks = 0;

public:
    // Run queued microtasks.
    void processTasks()
    {
        while (!microtasks.empty())
        {
            auto fn = std::move(microtasks.front());
            microtasks.pop_front();
            try { fn(); } catch (...) {}
            if (pendingMicrotasks > 0) --pendingMicrotasks;
        }
    }

private:
    void enqueueTask(std::function<void()> fn)
    {
        microtasks.emplace_back(std::move(fn));
        ++pendingMicrotasks;
    }

    void handlePush(RpcSessionData* sessionData, const json& pushData);
    protocol::Message handlePull(RpcSessionData* sessionData, int exportId);
    void handleRelease(RpcSessionData* sessionData, int exportId, int refcount);
    void handleAbort(RpcSessionData* sessionData, const json& errorData);
    json resolvePipelineReferences(RpcSessionData* sessionData, const json& value);

    // Allocate a new negative export ID for server-originated exports/promises.
    int allocateNegativeExportId(RpcSessionData* sessionData)
    {
        return sessionData->exporter.allocateNegativeExportId();
    }
};

} // namespace capnwebcpp
