#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "capnwebcpp/rpc_target.h"

namespace capnwebcpp
{

using json = nlohmann::json;

// Minimal StubHook-like abstraction for future client support and
// for server-originated callback capabilities.
class StubHook
{
public:
    virtual ~StubHook() = default;

    // Invoke a method with JSON args; returns JSON result.
    virtual json call(const std::string& method, const json& args) = 0;
};

// LocalTargetHook adapts a local RpcTarget to a StubHook.
class LocalTargetHook : public StubHook
{
public:
    explicit LocalTargetHook(std::shared_ptr<RpcTarget> target) : target(std::move(target)) {}

    json call(const std::string& method, const json& args) override
    {
        if (!target) return json();
        return target->dispatch(method, args);
    }

private:
    std::shared_ptr<RpcTarget> target;
};

inline std::shared_ptr<StubHook> makeLocalTargetHook(std::shared_ptr<RpcTarget> target)
{
    return std::make_shared<LocalTargetHook>(std::move(target));
}

} // namespace capnwebcpp

