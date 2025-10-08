#pragma once

#include <optional>
#include <nlohmann/json.hpp>

namespace capnwebcpp
{

using json = nlohmann::json;

// Returns true if v is an ["export", id] tuple per protocol.
inline bool isExportTuple(const json& v)
{
    return v.is_array() && v.size() >= 2 && v[0].is_string() && v[0] == "export" && v[1].is_number_integer();
}

// Extract a client-provided export ID from either an ["export", id] tuple or a {"$stub": id} marker.
inline std::optional<int> extractExportId(const json& v)
{
    if (isExportTuple(v)) return v[1].get<int>();
    if (v.is_object() && v.contains("$stub") && v["$stub"].is_number_integer()) return v["$stub"].get<int>();
    return std::nullopt;
}

} // namespace capnwebcpp

