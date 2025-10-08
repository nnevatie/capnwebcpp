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

} // namespace capnwebcpp

