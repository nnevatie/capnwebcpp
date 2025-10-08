#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "capnwebcpp/rpc_target.h"
#include "capnwebcpp/stub_hook.h"

namespace capnwebcpp
{
class RpcTransport;

using json = nlohmann::json;

// --------------------------------------------------------------------------------------
// Importer / Exporter roles (session state separation)

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

    // Hook on which to dispatch calls for this export (for server-originated exports/stubs).
    std::shared_ptr<StubHook> callHook;

    // Imported client references (IDs provided by the client in args/captures) used during this
    // export's lifetime. Will be released back to the client ("release" frames) when the export
    // completes (on pull resolution or reject).
    std::unordered_map<int, int> importedClientIds; // id -> refcount
};

// Import table entry scaffolding for future inbound exports/promises.
struct ImportEntry
{
    int localRefcount = 1;            // Our local references
    int remoteRefcount = 1;           // Remote-held references (peer believes we hold)
    bool hasResolution = false;       // True if resolve/reject received
    json resolution;                  // Resolved value or error tuple
};

// Manages ownership and lifecycle of server-side export entries.
class Exporter
{
public:
    int allocateForPush()
    {
        return nextExportId++;
    }

    int allocateNegativeExportId()
    {
        return nextExportIdNegative--;
    }

    ExportEntry* find(int id)
    {
        auto it = table.find(id);
        if (it == table.end()) return nullptr;
        return &it->second;
    }

    const ExportEntry* find(int id) const
    {
        auto it = table.find(id);
        if (it == table.end()) return nullptr;
        return &it->second;
    }

    void setOperation(int id, const std::string& method, const json& args, std::shared_ptr<StubHook> callHook)
    {
        ExportEntry entry;
        entry.remoteRefcount = 1;
        entry.hasOperation = true;
        entry.method = method;
        entry.args = args;
        entry.callHook = std::move(callHook);
        table[id] = std::move(entry);
    }

    void setResult(int id, const json& result)
    {
        auto it = table.find(id);
        if (it == table.end()) return;
        it->second.hasOperation = false;
        it->second.hasResult = true;
        it->second.result = result;
    }

    void cacheResult(int id, const json& result)
    {
        auto& e = table[id];
        e.hasResult = true;
        e.result = result;
        e.hasOperation = false;
        e.method.clear();
        e.args = json();
    }

    bool getResult(int id, json& out) const
    {
        auto it = table.find(id);
        if (it != table.end() && it->second.hasResult)
        {
            out = it->second.result;
            return true;
        }
        return false;
    }

    bool getOperation(int id, std::string& method, json& args) const
    {
        auto it = table.find(id);
        if (it != table.end() && it->second.hasOperation)
        {
            method = it->second.method;
            args = it->second.args;
            return true;
        }
        return false;
    }

    void put(int id, const ExportEntry& entry)
    {
        table[id] = entry;
    }

    void release(int id, int refcount)
    {
        auto it = table.find(id);
        if (it == table.end())
            return;
        if (refcount > 0)
            it->second.remoteRefcount -= refcount;
        if (it->second.remoteRefcount <= 0)
            table.erase(it);
    }

    void reset()
    {
        table.clear();
        nextExportId = 1;
        nextExportIdNegative = -1;
    }

    // Back-compat exposure for testing and transitional code.
    std::unordered_map<int, ExportEntry> table;

private:
    int nextExportId = 1;              // Aligned to client push order (for pull)
    int nextExportIdNegative = -1;     // Server-chosen negative IDs for exporter-originated
};

// Manages ownership and lifecycle of client-originated imports as seen by the server.
class Importer
{
public:
    int allocatePositiveImportId()
    {
        return nextImportId++;
    }

    // Record a resolution (resolve/reject) and return how many remote refs to release.
    int recordResolutionAndGetReleaseCount(int importId, const json& resolution)
    {
        auto& imp = table[importId];
        imp.hasResolution = true;
        imp.resolution = resolution;
        int releaseCount = imp.remoteRefcount > 0 ? imp.remoteRefcount : 1;
        table.erase(importId);
        return releaseCount;
    }

    // Utility used in tests to seed refcounts.
    void setRefcounts(int importId, int remoteRef, int localRef)
    {
        auto& imp = table[importId];
        imp.remoteRefcount = remoteRef;
        imp.localRefcount = localRef;
    }

    void reset()
    {
        table.clear();
        nextImportId = 1;
    }

    // Decrement local references for an import when the peer sends a release targeting an ID we
    // imported (defensive handling). Erase the entry when localRefcount reaches zero.
    void releaseLocal(int importId, int count)
    {
        auto it = table.find(importId);
        if (it == table.end()) return;
        if (count <= 0) return;
        if (it->second.localRefcount > 0)
        {
            it->second.localRefcount -= count;
            if (it->second.localRefcount <= 0)
            {
                table.erase(it);
            }
        }
    }

    // Back-compat exposure for tests and transitional code.
    std::unordered_map<int, ImportEntry> table;

private:
    int nextImportId = 1;             // Positive IDs we allocate when initiating calls
};

// Internal data associated with each connection/session.
struct RpcSessionData
{
    Exporter exporter;
    Importer importer;
    std::shared_ptr<RpcTarget> target;
    std::shared_ptr<RpcTransport> transport; // Optional persistent transport (e.g., WebSocket)

    // Map of our initiated import IDs to server-exported promise IDs for forwarding resolution.
    std::unordered_map<int, int> importToPromiseExport;

    // Canonical local call hook for the server target; used to re-export stubs consistently.
    std::shared_ptr<StubHook> localTargetHook;
    // Reverse export map: target instance pointer -> export ID (for per-target re-export parity).
    std::unordered_map<std::uintptr_t, int> targetExportId;
    // Registry of server target instances referenced by export markers.
    std::unordered_map<std::uintptr_t, std::shared_ptr<RpcTarget>> targetRegistry;

    // Back-compat field aliases for existing tests and code paths.
    std::unordered_map<int, ExportEntry>& exports;
    std::unordered_map<int, ImportEntry>& imports;

    RpcSessionData()
        : exports(exporter.table), imports(importer.table)
    {
    }
};

} // namespace capnwebcpp
