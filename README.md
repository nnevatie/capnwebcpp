# capnwebcpp
[![CI (Linux • macOS • Windows)](https://img.shields.io/github/actions/workflow/status/nnevatie/capnwebcpp/ci.yml?branch=main&label=CI%20(Linux%20%E2%80%A2%20macOS%20%E2%80%A2%20Windows)&logo=github)](https://github.com/nnevatie/capnwebcpp/actions/workflows/ci.yml)
[![Interop (Node ↔ capnweb)](https://img.shields.io/github/actions/workflow/status/nnevatie/capnwebcpp/ci.yml?branch=main&label=Interop%20(Node%20%E2%86%94%20capnweb)&logo=github)](https://github.com/nnevatie/capnwebcpp/actions/workflows/ci.yml)

[Cap'n Web](https://github.com/cloudflare/capnweb) C++ Server Library. This small library allows the user to create server implementations for the Cap'n Web RPC protocol, in delightful C++.

## Status

Server-focused implementation of the Cap'n Web RPC protocol with bidirectional calling support.

`capnwebcpp` provides WebSocket and HTTP-batch transports plus a MessagePort adapter, implements remap and a client-call path for server→client calls, and includes serialization hardening and robust lifecycle handling. A minimal C++ HTTP batch client is available; the library interoperates with the original TypeScript/JavaScript `capnweb` client.

| Feature | Status | Notes |
| --- | --- | --- |
| WebSocket endpoint | ✅ | uWebSockets-based server endpoint |
| HTTP-batch endpoint | ✅ | Server-side batch processing via POST; supports export-capture remap (promise placeholders; push/pull frames) |
| RpcTarget dispatch | ✅ | Method registry with JSON args/return |
| push/pull/resolve/reject | ✅ | Basic semantics wired end-to-end |
| Pipelining (property paths) | ✅ | Pipeline eval + property path resolution |
| Examples interop | ✅ | Helloworld and batch-pipelining with JS clients |
| Protocol message framing (parse/serialize) | ✅ | MessageType + parser/serializer wired into session |
| Serialization (JSON + extended types) | ✅ | Array escape; bigint/date/bytes/undefined/error encoding/decoding |
| Import/export tables + refcounts (neg ID policy) | ✅ | Re-export reuse; remoteRefcount tracked; negative IDs |
| Release semantics | ✅ | Auto-release on resolve/reject; export release decrements and erases at zero (aggregated supported) |
| Transport abstraction | ✅ | Interface + uWS/HTTP batch + MessagePort adapters |
| Client stubs/promises in C++ | ⚠️ | Minimal HTTP batch client + uWebSockets WebSocket client; basic stubs; limited promise handling |
| Advanced serialization (capnweb extended types) | ✅ | Supported via sentinel wrappers ($bigint/$date/$bytes/$undefined/$error) |
| Error redaction hooks | ✅ | onSendError hook finalized; applied to reject/abort with shape sanitization |
| Abort/onBroken callbacks | ✅ | Send abort frames, close transport, cleanup tables, propagate onBroken callbacks |
| Drain and stats | ✅ | drain() + getStats() implemented; batch awaits drain |
| `map`/`remap` instruction pipeline | ✅ | Supports pipeline/get/value/array/object/nested remap |
| Remap capture distinction | ✅ | Distinguishes ["import"] vs ["export"] captures |
| Client-call path for remap exports | ✅ | Calls captured exports (method/get) via push/pull; forwards resolution to promise |
| Public server→client call API | ✅ | `callClient()`/`callClientMethod()` for direct server-initiated calls |
| MessagePort transport | ✅ | In-process MessageChannel + transport adapter |
| Serialization hardening | ✅ | Depth guards; property path validation; reserved key sanitization |
| Server-originated exports/promises | ✅ | Emits ["export"/"promise", negId]; promise resolves on pull |
| Calls to exported stubs | ✅ | Pipelined calls and remap captured calls dispatch to correct target |
| Tests + CI | ✅ | Unit tests (RPC/protocol/serialize) and Linux/macOS/Windows CI |

Status icons: ✅ implemented, ⚠️ partial, ❌ not yet implemented

Ongoing Work
- Public API polish: diagnostics (stats/logging)
- Workers/Node convenience helpers and transport adapters
- Cross-interop and fuzz tests against capnweb
- Performance: microtask scheduling and pipeline caching
- Documentation: batch-mode limitations and server→client examples
- Expand C++ client: WebSocket transport, stub lifecycle/release, promise awaiting

## Dependencies

* CMake
* A C++20 -compatible compiler
* [uWebSockets](https://github.com/uNetworking/uWebSockets), bundled
* [nlohmann/json](https://github.com/nlohmann/json), bundled

## Building (with Ninja)

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

### MessagePort (in-process)
```
examples\\messageport\\messageport ..
```
This example uses an in-process MessageChannel to simulate client/server over a MessagePort transport.

### WebSocket Callback (server→client calls)
```
examples\\websocket-callback\\websocket-callback ..
```
Open a [WebSocket callback client](http://localhost:8000/static/examples/websocket-callback/index.html) in a browser. The server calls back to the client’s RPC target using the server→client call API.

## C++ Client (HTTP Batch)

Use the minimal batch client to call a remote server from C++. Provide a transport function that takes outbound frames and returns the server’s responses.

Example:

```
#include <capnwebcpp/client_api.h>

using namespace capnwebcpp;

// Implement this to POST batch.join("\n") to your server and split("\n") the response.
std::vector<std::string> sendBatch(const std::vector<std::string>& batch);

int main() {
    auto transport = std::make_shared<FuncBatchTransport>(sendBatch);
    RpcClient client(transport);
    auto result = client.callMethod("hello", nlohmann::json::array({"World"}));
    // result == "Hello, World!"
}
```

Stub results can be called using `callStubMethod()` / `getStubProperty()` with the returned `{ "$stub": id }`.

### C++ Client (WebSocket, persistent)

Use the uWebSockets-based persistent client to keep a WebSocket open and issue multiple calls:

```
#include <capnwebcpp/client_ws.h>

using namespace capnwebcpp;

int main() {
    RpcWsClient client("ws://127.0.0.1:8000/api");
    auto res = client.callMethod("hello", nlohmann::json::array({"World"}));
    // res == "Hello, World!"
}
```

Note: Current client is synchronous and sends a `release` after each resolve. Advanced features (promise awaiting, batched pipelining, bidirectional callbacks) are limited and will be expanded.
