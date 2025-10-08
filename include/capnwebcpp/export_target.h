#pragma once

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>

#include "capnwebcpp/rpc_session.h"

namespace capnwebcpp
{

using json = nlohmann::json;

// Register a target instance for export and return a sentinel value to be placed in results.
// On devaluation, this will be converted into an ["export", negId] with stable identity per target.
inline json exportTarget(RpcSessionData* sessionData, std::shared_ptr<RpcTarget> target)
{
    if (!sessionData) return json();
    std::uintptr_t key = reinterpret_cast<std::uintptr_t>(target.get());
    sessionData->targetRegistry[key] = std::move(target);
    return json{ {"$export_target_ptr", key} };
}

} // namespace capnwebcpp

