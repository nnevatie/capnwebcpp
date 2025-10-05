#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "capnwebcpp/rpc_target.h"
#include "capnwebcpp/protocol.h"

namespace capnwebcpp
{

using json = nlohmann::json;

// Export table entry, tracks either a pending operation or a computed result,
// along with the remote refcount for release semantics.
struct ExportEntry
{
    int remoteRefcount = 1;           // Remote-held references to this export
    int localRefcount = 1;            // Local references (future use)
    bool hasResult = false;
    json result;                      // Valid if hasResult

    bool hasOperation = false;
    std::string method;               // Valid if hasOperation
    json args;                        // Valid if hasOperation

    // Target on which to dispatch calls for this export (for server-originated exports/stubs).
    std::shared_ptr<RpcTarget> callTarget;
};

// Import table entry scaffolding for future inbound exports/promises.
struct ImportEntry
{
    int localRefcount = 1;            // Our local references
    int remoteRefcount = 1;           // Remote-held references (peer believes we hold)
    bool hasResolution = false;       // True if resolve/reject received
    json resolution;                  // Resolved value or error tuple
};

// Internal data associated with each connection/session.
struct RpcSessionData
{
    std::unordered_map<int, ExportEntry> exports; // exportId -> entry
    std::unordered_map<int, ImportEntry> imports; // importId -> entry (scaffolding)
    int nextExportId = 1;                         // Export IDs aligned to client push order (for pull)
    int nextExportIdNegative = -1;                // Server-chosen negative IDs for exporter-originated
    int nextImportId = 1;                         // Import IDs we may allocate if we send pushes
    std::shared_ptr<RpcTarget> target;
};

// RpcSession handles the Cap'n Web RPC protocol for a connection.
class RpcSession
{
public:
    explicit RpcSession(std::shared_ptr<RpcTarget> target) : target(target) {}

    // Handle incoming message; returns a response (possibly empty).
    std::string handleMessage(RpcSessionData* sessionData, const std::string& message);

    // Connection lifecycle hooks.
    void onOpen(RpcSessionData* sessionData);
    void onClose(RpcSessionData* sessionData);

    // Lifecycle: return true if there are no outstanding pulls to resolve.
    bool isDrained() const { return pullCount == 0; }
    bool isAborted() const { return aborted; }

    // Internal onBroken registration (reserved for future use).
    void registerOnBroken(std::function<void(const std::string&)> cb) { onBrokenCallbacks.push_back(std::move(cb)); }

private:
    std::shared_ptr<RpcTarget> target;

    // Lifecycle state
    int pullCount = 0;
    bool aborted = false;
    std::vector<std::function<void(const std::string&)>> onBrokenCallbacks;

    void handlePush(RpcSessionData* sessionData, const json& pushData);
    protocol::Message handlePull(RpcSessionData* sessionData, int exportId);
    void handleRelease(RpcSessionData* sessionData, int exportId, int refcount);
    void handleAbort(RpcSessionData* sessionData, const json& errorData);
    json resolvePipelineReferences(RpcSessionData* sessionData, const json& value);

    // Allocate a new negative export ID for server-originated exports/promises.
    int allocateNegativeExportId(RpcSessionData* sessionData)
    {
        return sessionData->nextExportIdNegative--;
    }
};

} // namespace capnwebcpp
