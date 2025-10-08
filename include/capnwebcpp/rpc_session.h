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
    // Optional: rewrite error tuples before sending (e.g., redaction).
    // Contract:
    // - The callback receives an error tuple of the form ["error", name, message, optional stack].
    // - It may return a rewritten tuple. If the return value is malformed, a sanitized
    //   ["error", string name, string message, optional string stack] is sent.
    // - Applied to outbound reject frames produced by the server and to abort payloads.
    // - Not applied when merely forwarding a peer-provided reject (e.g., forwarded client errors).
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

    // Emit release frames for any imported client refs associated with outstanding exports.
    void emitPendingReleases(RpcSessionData* sessionData, RpcTransport& transport);

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

    // ----------------------------------------------------------------------------
    // Public server-to-client call API
    // Initiate a call to a client-exported stub (`exportId` from the client's perspective),
    // identified here by the same numeric ID (as used in remap export captures). The call is
    // transmitted over the persistent transport as a push followed by an immediate pull of the
    // newly allocated import ID. Returns a negative export ID for a promise which will be
    // resolved proactively when the client responds.
    int callClient(RpcSessionData* sessionData, int exportId, const json& path, const json& args = json());
    int callClientMethod(RpcSessionData* sessionData, int exportId, const std::string& method, const json& argsArray);

    // Link a client-exported promise (importId from our perspective) to a newly-exported
    // negative promise ID. When a resolve/reject for importId arrives, it will be forwarded to the
    // returned negative export ID. Does not send any messages; the peer will resolve proactively.
    int awaitClientPromise(RpcSessionData* sessionData, int importId);

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

    // Apply the configured redaction callback to an error tuple and sanitize shape.
    json redactError(const json& err)
    {
        // Only handle tuples of the form ["error", name, message, optional stack]
        if (!err.is_array() || err.size() < 3 || !err[0].is_string() || err[0] != "error")
            return err;

        auto sanitize = [](const json& in) -> json
        {
            if (!in.is_array() || in.size() < 3) return json::array({ "error", "Error", "(redacted)" });
            json out = json::array();
            out.push_back("error");
            // name
            if (in.size() >= 2 && in[1].is_string()) out.push_back(in[1]);
            else out.push_back("Error");
            // message
            if (in.size() >= 3 && in[2].is_string()) out.push_back(in[2]);
            else out.push_back("(redacted)");
            // optional stack
            if (in.size() >= 4 && in[3].is_string()) out.push_back(in[3]);
            return out;
        };

        if (!onSendError)
            return sanitize(err);

        try
        {
            json rewritten = onSendError(err);
            return sanitize(rewritten);
        }
        catch (...)
        {
            return sanitize(err);
        }
    }
};

} // namespace capnwebcpp
