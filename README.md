# capnwebcpp
[![CI (Linux • macOS • Windows)](https://img.shields.io/github/actions/workflow/status/nnevatie/capnwebcpp/ci.yml?branch=main&label=CI%20(Linux%20%E2%80%A2%20macOS%20%E2%80%A2%20Windows)&logo=github)](https://github.com/nnevatie/capnwebcpp/actions/workflows/ci.yml)
[![Interop (Node ↔ capnweb)](https://img.shields.io/github/actions/workflow/status/nnevatie/capnwebcpp/ci.yml?branch=main&label=Interop%20(Node%20%E2%86%94%20capnweb)&logo=github)](https://github.com/nnevatie/capnwebcpp/actions/workflows/ci.yml)

[Cap'n Web](https://github.com/cloudflare/capnweb) C++ library. Build Cap'n Web RPC services in C++ with uWebSockets/WebSocket, HTTP batch, and MessagePort transports, robust serialization and lifecycle handling, plus a minimal C++ client (HTTP batch and WebSocket) for calling Cap'n Web endpoints.

## Status

Server-focused implementation of the Cap'n Web RPC protocol with bidirectional calling support.

`capnwebcpp` provides WebSocket and HTTP-batch transports plus a MessagePort adapter, implements remap and a client-call path for server→client calls, and includes serialization hardening and robust lifecycle handling. A minimal C++ HTTP batch client and a uWebSockets-based WebSocket client are available; the library interoperates with the original TypeScript/JavaScript `capnweb` client.

| Area | Status |
| --- | --- |
| Core protocol (push/pull/resolve/reject/release/abort) | ✅ |
| Serialization + hardening | ✅ |
| Remap / pipelining | ✅ |
| Transports (WebSocket, HTTP batch, MessagePort) | ✅ |
| Server→client calls | ✅ |
| Import/export tables + refcounts | ✅ |
| Lifecycle (abort, drain, stats) | ✅ |
| C++ client (HTTP batch + WebSocket) | ⚠️ |
| Interop & tests | ✅ |

Status icons: ✅ implemented, ⚠️ partial, ❌ not yet implemented

Ongoing Work
- Diagnostics/logging: richer stats, tracing hooks
- C++ client: async API, stub lifecycle/release, promise awaiting/pipelining, wss/TLS, reconnection
- Additional adapters: Workers/Node helpers and transports
- Interop: broader coverage and fuzz tests against capnweb
- Performance: microtask scheduling, caching, event-loop integration
- Docs: client usage (batch/WS), wss setup, batch caveats, server→client examples

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
