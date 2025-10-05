#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include <functional>

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

// --------------------------------------------------------------------------------------
// Evaluator / Devaluator-style processing (subset)

class Devaluator
{
public:
    // For now, devaluator is trivial as server values are plain JSON.
    static json devaluate(const json& value) { return value; }
};

class Evaluator
{
public:
    using ResultGetter = std::function<bool(int /*exportId*/, json& /*outResult*/)>;
    using OperationGetter = std::function<bool(int /*exportId*/, std::string& /*method*/, json& /*args*/)>;
    using Dispatcher = std::function<json(const std::string& /*method*/, const json& /*args*/)>;
    using Cacher = std::function<void(int /*exportId*/, const json& /*result*/)>;

    // Evaluate a value tree, resolving any ["pipeline", exportId, path?] references by using
    // callbacks to fetch or compute results, then traversing property paths.
    static json evaluateValue(const json& value,
                              const ResultGetter& getResult,
                              const OperationGetter& getOperation,
                              const Dispatcher& dispatch,
                              const Cacher& cache);
};

} // namespace serialize

} // namespace capnwebcpp
