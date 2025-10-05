#include "capnwebcpp/serialize.h"

namespace capnwebcpp
{
namespace serialize
{

json wrapArrayIfNeeded(const json& value)
{
    if (value.is_array())
        return json::array({ value });
    return value;
}

json makeError(const std::string& name, const std::string& message)
{
    return json::array({ "error", name, message });
}

json resolveFrame(int exportId, const json& value)
{
    return json::array({ "resolve", exportId, wrapArrayIfNeeded(value) });
}

json rejectFrame(int exportId, const json& errorTuple)
{
    return json::array({ "reject", exportId, errorTuple });
}

static json traversePath(json result, const json& path)
{
    if (!path.is_array())
        return result;
    for (const auto& key : path)
    {
        if (key.is_string() && result.is_object())
        {
            result = result[key.get<std::string>()];
        }
        else if (key.is_number() && result.is_array())
        {
            result = result[key.get<int>()];
        }
    }
    return result;
}

json Evaluator::evaluateValue(const json& value,
                              const ResultGetter& getResult,
                              const OperationGetter& getOperation,
                              const Dispatcher& dispatch,
                              const Cacher& cache)
{
    if (value.is_array())
    {
        if (value.size() >= 2 && value[0].is_string() && value[0] == "pipeline" && value[1].is_number())
        {
            int exportId = value[1];

            json result;
            if (getResult(exportId, result))
            {
                // Use cached result, then apply optional path.
                if (value.size() >= 3)
                    result = traversePath(result, value[2]);
                return result;
            }
            else
            {
                std::string method;
                json args;
                if (!getOperation(exportId, method, args))
                {
                    throw std::runtime_error("Pipeline reference to non-existent export: " + std::to_string(exportId));
                }

                // Resolve arguments (may contain pipeline references as well).
                json resolvedArgs = evaluateValue(args, getResult, getOperation, dispatch, cache);

                json computed = dispatch(method, resolvedArgs);
                cache(exportId, computed);

                if (value.size() >= 3)
                    computed = traversePath(computed, value[2]);
                return computed;
            }
        }
        else
        {
            json resolved = json::array();
            for (const auto& elem : value)
            {
                resolved.push_back(evaluateValue(elem, getResult, getOperation, dispatch, cache));
            }
            return resolved;
        }
    }
    else if (value.is_object())
    {
        json resolved = json::object();
        for (auto& [key, val] : value.items())
        {
            resolved[key] = evaluateValue(val, getResult, getOperation, dispatch, cache);
        }
        return resolved;
    }
    else
    {
        return value;
    }
}

} // namespace serialize
} // namespace capnwebcpp
