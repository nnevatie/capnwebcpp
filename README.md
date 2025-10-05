# capnwebcpp
[![CI (Linux • macOS • Windows)](https://img.shields.io/github/actions/workflow/status/nnevatie/capnwebcpp/ci.yml?branch=main&label=CI%20(Linux%20%E2%80%A2%20macOS%20%E2%80%A2%20Windows)&logo=github)](https://github.com/nnevatie/capnwebcpp/actions/workflows/ci.yml)

[Cap'n Web](https://github.com/cloudflare/capnweb) C++ Server Library. This small library allows the user to create server implementations for the Cap'n Web RPC protocol, in delightful C++.

The library does not offer many conveniences - it only supplies the minimal plumbing required for getting messages flying between the RPC client and server.

## Status

Early prototype of a server-side subset of the Cap'n Web protocol. The library currently provides a minimal but working implementation that interoperates with the original TypeScript/JavaScript capnweb client.

| Feature | Status | Notes |
| --- | --- | --- |
| WebSocket endpoint | ✅ | uWebSockets-based server endpoint |
| HTTP-batch endpoint | ✅ | Server-side batch processing via POST |
| RpcTarget dispatch | ✅ | Method registry with JSON args/return |
| push/pull/resolve/reject | ✅ | Basic semantics wired end-to-end |
| Pipelining (property paths) | ⚠️ | Limited support via lazy evaluation |
| Examples interop | ✅ | Helloworld and batch-pipelining with JS clients |
| Protocol message framing (parse/serialize) | ✅ | MessageType + parser/serializer wired into session |
| Basic serialization helpers | ✅ | Array escape + error tuples; used for resolve/reject |
| Import/export tables + refcounts (neg ID policy) | ⚠️ | Tables with local/remote refcounts; negative IDs for server exports/promises |
| Release semantics | ⚠️ | Import release sent on resolve/reject; export release decrements remote refs |
| Transport abstraction | ⚠️ | Interface + uWS/batch adapters; no Workers/MessagePort yet |
| Client stubs/promises in C++ | ❌ | Server-only library |
| Advanced serialization (bigint/date/bytes/undefined) | ❌ | Special types not supported; basic wrappers present |
| Error redaction hooks | ❌ | No `onSendError` equivalent |
| Abort/onBroken callbacks | ❌ | Basic logging only |
| `drain()` and stats | ❌ | No coordination APIs yet |
| `map`/`remap` instruction pipeline | ❌ | Not recognized/processed |
| Server-originated exports/promises | ✅ | Emits ["export"/"promise", negId]; promise resolves on pull |
| Calls to exported stubs | ✅ | Pipelined calls dispatch to bound target via importId |
| Tests + CI | ✅ | Unit tests (RPC/protocol/serialize) and Linux/macOS/Windows CI |

Status icons: ✅ implemented, ⚠️ partial, ❌ not yet implemented

Ongoing Work
- Refactoring toward the TS/JS architecture (separating session/core/transport)
- Improving protocol conformance and resource management

## Dependencies

* CMake
* A C++20 -compatible compiler
* [uWebSockets](https://github.com/uNetworking/uWebSockets), bundled
* [nlohmann/json](https://github.com/nlohmann/json), bundled

## Building (with Ninja)

Building the library has only been tested on MSVC 2022, so far.

```
git clone https://github.com/nnevatie/capnwebcpp.git
cd capnwebcpp
mkdir build
cd build
cmake -G Ninja ..
ninja
```

## Running Examples

### Helloworld
```
examples\helloworld\helloworld ..
```
Open a [helloworld client](http://localhost:8000/static/examples/helloworld/index.html) in a browser.

### Batch-pipelining
```
examples\batch-pipelining\batch-pipelining ..
```
Open a [batch-pipelining](http://localhost:8000/static/examples/batch-pipelining/index.html) in a browser.

