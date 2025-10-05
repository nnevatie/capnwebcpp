#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace capnwebcpp
{

using json = nlohmann::json;

namespace serialize
{

// Wraps arrays in an outer single-element array to escape them per protocol.
json wrapArrayIfNeeded(const json& value);

// Build an error tuple: ["error", name, message] (stack optional, omitted for now).
json makeError(const std::string& name, const std::string& message);

// Build resolve / reject frames (JSON arrays) ready for protocol serialization.
json resolveFrame(int exportId, const json& value);
json rejectFrame(int exportId, const json& errorTuple);

} // namespace serialize

} // namespace capnwebcpp

