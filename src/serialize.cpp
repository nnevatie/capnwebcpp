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

bool isSpecialArray(const json& value)
{
    if (!value.is_array() || value.empty() || !value[0].is_string())
        return false;
    const std::string tag = value[0];
    return tag == "export" || tag == "promise" || tag == "error" || tag == "bigint" ||
           tag == "date" || tag == "bytes" || tag == "undefined" || tag == "import" ||
           tag == "pipeline" || tag == "remap";
}

static json devaluateObjectForResult(const json& value, const std::function<int(bool,const json&)>& newExportId);

static json devaluateArrayForResult(const json& arr, const std::function<int(bool,const json&)>& newExportId)
{
    json out = json::array();
    for (const auto& el : arr)
    {
        if (el.is_object()) out.push_back(devaluateObjectForResult(el, newExportId));
        else if (el.is_array()) out.push_back(devaluateArrayForResult(el, newExportId));
        else out.push_back(el);
    }
    return out;
}

static json devaluateObjectForResult(const json& obj, const std::function<int(bool,const json&)>& newExportId)
{
    // Special sentinel objects to request export/promise emission.
    if (obj.is_object())
    {
        // Extended types
        auto itBig = obj.find("$bigint");
        if (itBig != obj.end() && itBig->is_string())
        {
            return json::array({ "bigint", *itBig });
        }
        auto itDate = obj.find("$date");
        if (itDate != obj.end() && (itDate->is_number_integer() || itDate->is_number()))
        {
            return json::array({ "date", *itDate });
        }
        auto itBytes = obj.find("$bytes");
        if (itBytes != obj.end() && itBytes->is_string())
        {
            return json::array({ "bytes", *itBytes });
        }
        auto itUndef = obj.find("$undefined");
        if (itUndef != obj.end() && itUndef->is_boolean() && *itUndef)
        {
            return json::array({ "undefined" });
        }
        auto itErr = obj.find("$error");
        if (itErr != obj.end() && itErr->is_object())
        {
            const json& eobj = *itErr;
            std::string name = eobj.value("name", std::string("Error"));
            std::string message = eobj.value("message", std::string(""));
            json arr = json::array({ "error", name, message });
            if (eobj.contains("stack") && eobj["stack"].is_string())
                arr.push_back(eobj["stack"]);
            return arr;
        }

        auto itExp = obj.find("$export");
        if (itExp != obj.end() && itExp->is_boolean() && *itExp)
        {
            int id = newExportId(false, json());
            return json::array({ "export", id });
        }
        auto itProm = obj.find("$promise");
        if (itProm != obj.end())
        {
            if (itProm->is_boolean() && *itProm)
            {
                int id = newExportId(true, json());
                return json::array({ "promise", id });
            }
            else
            {
                // If $promise holds a value, use that as the resolution payload.
                int id = newExportId(true, *itProm);
                return json::array({ "promise", id });
            }
        }
    }

    // Otherwise, recursively devaluate fields.
    json out = json::object();
    for (auto& [k, v] : obj.items())
    {
        if (v.is_object()) out[k] = devaluateObjectForResult(v, newExportId);
        else if (v.is_array()) out[k] = devaluateArrayForResult(v, newExportId);
        else out[k] = v;
    }
    return out;
}

json devaluateForResult(const json& value, const std::function<int(bool,const json&)>& newExportId)
{
    if (value.is_object())
        return devaluateObjectForResult(value, newExportId);
    if (value.is_array())
        return devaluateArrayForResult(value, newExportId);
    return value;
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
        // Handle extended types first
        if (value.size() >= 1 && value[0].is_string())
        {
            const std::string tag = value[0];
            if (tag == "remap")
            {
                throw std::runtime_error("remap is not supported in this build");
            }
            if (tag == "bigint")
            {
                if (value.size() >= 2 && value[1].is_string())
                    return json{ {"$bigint", value[1]} };
            }
            else if (tag == "date")
            {
                if (value.size() >= 2 && (value[1].is_number() || value[1].is_number_integer()))
                    return json{ {"$date", value[1]} };
            }
            else if (tag == "bytes")
            {
                if (value.size() >= 2 && value[1].is_string())
                    return json{ {"$bytes", value[1]} };
            }
            else if (tag == "undefined")
            {
                return json{ {"$undefined", true} };
            }
            else if (tag == "error")
            {
                // ["error", name, message, optional stack]
                if (value.size() >= 3 && value[1].is_string() && value[2].is_string())
                {
                    json e = json::object({
                        {"name", value[1].get<std::string>()},
                        {"message", value[2].get<std::string>()}
                    });
                    if (value.size() >= 4 && value[3].is_string())
                        e["stack"] = value[3].get<std::string>();
                    return json{ {"$error", e} };
                }
            }
        }
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
