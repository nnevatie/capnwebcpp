#pragma once

#include <string>
#include <nlohmann/json.hpp>

#include "capnwebcpp/rpc_session.h"

namespace capnwebcpp
{

using json = nlohmann::json;

inline json makeClientStub(int exportId)
{
    return json{ {"$stub", exportId} };
}

inline bool isClientStub(const json& v)
{
    return v.is_object() && v.contains("$stub") && v["$stub"].is_number_integer();
}

inline int getClientStubId(const json& v)
{
    return isClientStub(v) ? v["$stub"].get<int>() : 0;
}

inline int callClientStubMethod(RpcSession* session, RpcSessionData* sessionData,
                                const json& stub, const std::string& method, const json& argsArray)
{
    int id = getClientStubId(stub);
    if (id == 0) throw std::runtime_error("not a client stub");
    return session->callClientMethod(sessionData, id, method, argsArray);
}

inline int getClientStubProperty(RpcSession* session, RpcSessionData* sessionData,
                                 const json& stub, const json& path)
{
    int id = getClientStubId(stub);
    if (id == 0) throw std::runtime_error("not a client stub");
    return session->callClient(sessionData, id, path);
}

inline bool isClientPromiseStub(const json& v)
{
    return v.is_object() && v.contains("$promise_stub") && v["$promise_stub"].is_number_integer();
}

inline int getClientPromiseStubId(const json& v)
{
    return isClientPromiseStub(v) ? v["$promise_stub"].get<int>() : 0;
}

inline int awaitClientPromiseStub(RpcSession* session, RpcSessionData* sessionData, const json& v)
{
    int id = getClientPromiseStubId(v);
    if (id == 0) throw std::runtime_error("not a client promise stub");
    return session->awaitClientPromise(sessionData, id);
}

// Convenience: produce a promise expression ["promise", negId] suitable for returning in a
// server result, by linking the client promise import to a newly-exported negative ID.
inline json awaitClientPromiseAsResult(RpcSession* session, RpcSessionData* sessionData, const json& v)
{
    int negId = awaitClientPromiseStub(session, sessionData, v);
    return json::array({ "promise", negId });
}

} // namespace capnwebcpp
