# capnwebcpp
[Cap'n Web](https://github.com/cloudflare/capnweb) C++ Server Library. This small library allows the user to create server implementations for the Cap'n Web RPC protocol, in delightful C++.

The library does not offer many conveniences - it only supplies the minimal plumbing required for getting messages flying between the RPC client and server.

## Status

Pre-alpha - hacked together in a couple of hours. Next steps are to verify protocol correctness and to clean up the messy implementation.

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

