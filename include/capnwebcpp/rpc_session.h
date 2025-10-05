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
    int refcount = 1;                 // Remote-held references
    bool hasResult = false;
    json result;                      // Valid if hasResult

    bool hasOperation = false;
    std::string method;               // Valid if hasOperation
    json args;                        // Valid if hasOperation
};

// Internal data associated with each connection/session.
struct RpcSessionData
{
    std::unordered_map<int, ExportEntry> exports; // exportId -> entry
    int nextExportId = 1;                         // Server-chosen export IDs (simplified)
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

private:
    std::shared_ptr<RpcTarget> target;

    void handlePush(RpcSessionData* sessionData, const json& pushData);
    protocol::Message handlePull(RpcSessionData* sessionData, int exportId);
    void handleRelease(RpcSessionData* sessionData, int exportId, int refcount);
    void handleAbort(RpcSessionData* sessionData, const json& errorData);
    json resolvePipelineReferences(RpcSessionData* sessionData, const json& value);
};

} // namespace capnwebcpp
