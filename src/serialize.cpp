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
                // ["remap", exportId, path, captures, instructions]
                if (value.size() != 5 || !value[1].is_number() || !value[2].is_array() ||
                    !value[3].is_array() || !value[4].is_array())
                {
                    throw std::runtime_error("invalid remap expression");
                }

                int baseExportId = value[1];
                const json& basePath = value[2];
                const json& captures = value[3];
                const json& instructions = value[4];

                // Build capture vector of export IDs from sender's perspective (our exports table).
                struct Cap { bool isImport; int id; };
                std::vector<Cap> capturesVec;
                for (const auto& cap : captures)
                {
                    if (!cap.is_array() || cap.size() != 2 || !cap[0].is_string() || !cap[1].is_number())
                        throw std::runtime_error("invalid remap capture");
                    std::string capTag = cap[0];
                    int id = cap[1];
                    if (capTag != "import" && capTag != "export")
                        throw std::runtime_error("unknown remap capture tag");
                    capturesVec.push_back(Cap{capTag == "import", id});
                }

                // Resolve the base input value from the export + path using pipeline evaluation.
                json basePipeline = json::array({ "pipeline", baseExportId, basePath });
                json input;
                try {
                    input = evaluateValue(basePipeline, getResult, getOperation, dispatch, cache);
                } catch (...) {
                    // If base reference is not meaningful in this context, treat input as null.
                    input = json();
                }

                std::vector<json> variables;
                variables.push_back(input);

                auto applyPathGet = [](json subject, const json& path) -> json
                {
                    if (!path.is_array()) return subject;
                    for (const auto& key : path)
                    {
                        if (key.is_string() && subject.is_object())
                            subject = subject[key.get<std::string>()];
                        else if (key.is_number() && subject.is_array())
                            subject = subject[key.get<int>()];
                    }
                    return subject;
                };

                for (const auto& instr : instructions)
                {
                    if (!instr.is_array() || instr.empty() || !instr[0].is_string())
                        throw std::runtime_error("invalid remap instruction");
                    std::string itag = instr[0].get<std::string>();
                    if (itag == "pipeline")
                    {
                        if (instr.size() < 3 || !instr[1].is_number() || !instr[2].is_array())
                            throw std::runtime_error("invalid pipeline instruction");
                        int subjectIdx = instr[1];
                        const json& path = instr[2];
                        bool hasArgs = instr.size() >= 4;
                        json args = hasArgs ? instr[3] : json::array();

                        json resultVal;
                        if (subjectIdx < 0)
                        {
                            int capIndex = -subjectIdx - 1;
                            if (capIndex < 0 || capIndex >= (int)capturesVec.size())
                                throw std::runtime_error("remap capture index out of range");
                            const Cap& c = capturesVec[capIndex];
                            if (!c.isImport)
                                throw std::runtime_error("remap pipeline on export capture not supported");
                            // Without a caller hook here, remap with captured calls must be evaluated in
                            // evaluateValueWithCaller(), not this path.
                            json resolvedArgs = hasArgs ? evaluateValue(args, getResult, getOperation, dispatch, cache) : json::array();
                            if (!path.is_array() || path.empty() || !path[0].is_string())
                                throw std::runtime_error("remap pipeline invalid method path");
                            std::string method = path[0];
                            // Treat like a local dispatch of the captured import's method name.
                            resultVal = dispatch(method, resolvedArgs);
                        }
                        else
                        {
                            if (subjectIdx >= (int)variables.size())
                                throw std::runtime_error("remap variable index out of range");
                            json subj = variables[subjectIdx];
                            // For local JSON values, only support property get (ignore args).
                            resultVal = applyPathGet(subj, path);
                        }

                        variables.push_back(resultVal);
                    }
                    else if (itag == "value")
                    {
                        // Push a literal value (evaluate recursively to allow expressions).
                        if (instr.size() != 2)
                            throw std::runtime_error("invalid value instruction");
                        variables.push_back(evaluateValue(instr[1], getResult, getOperation, dispatch, cache));
                    }
                    else if (itag == "get")
                    {
                        // Read a property path from a subject (local var or capture).
                        if (instr.size() != 3 || !instr[1].is_number() || !instr[2].is_array())
                            throw std::runtime_error("invalid get instruction");
                        int subjectIdx = instr[1];
                        const json& path = instr[2];
                        json resultVal;
                        if (subjectIdx < 0)
                        {
                            int capIndex = -subjectIdx - 1;
                            if (capIndex < 0 || capIndex >= (int)capturesVec.size())
                                throw std::runtime_error("remap capture index out of range");
                            const Cap& c = capturesVec[capIndex];
                            if (!c.isImport)
                                throw std::runtime_error("remap get on export capture not supported");
                            int expId = c.id;
                            json expr = json::array({ "pipeline", expId, path });
                            resultVal = evaluateValue(expr, getResult, getOperation, dispatch, cache);
                        }
                        else
                        {
                            if (subjectIdx >= (int)variables.size())
                                throw std::runtime_error("remap variable index out of range");
                            resultVal = applyPathGet(variables[subjectIdx], path);
                        }
                        variables.push_back(resultVal);
                    }
                    else if (itag == "array")
                    {
                        if (instr.size() != 2 || !instr[1].is_array())
                            throw std::runtime_error("invalid array instruction");
                        json out = json::array();
                        for (const auto& elem : instr[1])
                        {
                            if (elem.is_array() && elem.size() == 2 && elem[0].is_string() && elem[0] == "value")
                            {
                                out.push_back(evaluateValue(elem[1], getResult, getOperation, dispatch, cache));
                            }
                            else
                            {
                                out.push_back(evaluateValue(elem, getResult, getOperation, dispatch, cache));
                            }
                        }
                        variables.push_back(out);
                    }
                    else if (itag == "object")
                    {
                        if (instr.size() != 2 || !instr[1].is_array())
                            throw std::runtime_error("invalid object instruction");
                        json out = json::object();
                        for (const auto& kv : instr[1])
                        {
                            if (!kv.is_array() || kv.size() != 2 || !kv[0].is_string())
                                throw std::runtime_error("invalid object entry");
                            std::string key = kv[0].get<std::string>();
                            const auto& vexpr = kv[1];
                            if (vexpr.is_array() && vexpr.size() == 2 && vexpr[0].is_string() && vexpr[0] == "value")
                                out[key] = evaluateValue(vexpr[1], getResult, getOperation, dispatch, cache);
                            else
                                out[key] = evaluateValue(vexpr, getResult, getOperation, dispatch, cache);
                        }
                        variables.push_back(out);
                    }
                    else if (itag == "remap")
                    {
                        // Support nested remap by evaluating the full expression.
                        json nested = evaluateValue(instr, getResult, getOperation, dispatch, cache);
                        variables.push_back(nested);
                    }
                    else
                    {
                        throw std::runtime_error("unsupported remap instruction tag");
                    }
                }

                if (variables.empty())
                    return json();
                else
                    return variables.back();
            }
            else if (tag == "value")
            {
                // Expression wrapper: evaluate and return the inner value.
                if (value.size() != 2)
                    throw std::runtime_error("invalid value expression");
                return evaluateValue(value[1], getResult, getOperation, dispatch, cache);
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

json Evaluator::evaluateValueWithCaller(const json& value,
                              const ResultGetter& getResult,
                              const OperationGetter& getOperation,
                              const Dispatcher& dispatch,
                              const Cacher& cache,
                              const ExportCaller& callExport)
{
    // Local recursive evaluator that routes captured pipelines via callExport.
    std::function<json(const json&)> eval = [&](const json& v) -> json
    {
        if (v.is_array())
        {
            if (v.size() >= 1 && v[0].is_string())
            {
                const std::string tag = v[0];
                if (tag == "remap")
                {
                    if (v.size() != 5 || !v[1].is_number() || !v[2].is_array() ||
                        !v[3].is_array() || !v[4].is_array())
                        throw std::runtime_error("invalid remap expression");

                    int baseExportId = v[1];
                    const json& basePath = v[2];
                    const json& captures = v[3];
                    const json& instructions = v[4];

                    struct Cap { bool isImport; int id; };
                    std::vector<Cap> capturesVec;
                    for (const auto& cap : captures)
                    {
                        if (!cap.is_array() || cap.size() != 2 || !cap[0].is_string() || !cap[1].is_number())
                            throw std::runtime_error("invalid remap capture");
                        std::string capTag = cap[0];
                        int id = cap[1];
                        if (capTag != "import" && capTag != "export")
                            throw std::runtime_error("unknown remap capture tag");
                        capturesVec.push_back(Cap{capTag == "import", id});
                    }

                    json input;
                    try {
                        input = eval(json::array({"pipeline", baseExportId, basePath}));
                    } catch (...) { input = json(); }

                    std::vector<json> variables;
                    variables.push_back(input);

                    auto applyPathGet = [](json subject, const json& path) -> json
                    {
                        if (!path.is_array()) return subject;
                        for (const auto& key : path)
                        {
                            if (key.is_string() && subject.is_object())
                                subject = subject[key.get<std::string>()];
                            else if (key.is_number() && subject.is_array())
                                subject = subject[key.get<int>()];
                        }
                        return subject;
                    };

                    for (const auto& instr : instructions)
                    {
                        if (!instr.is_array() || instr.empty() || !instr[0].is_string())
                            throw std::runtime_error("invalid remap instruction");
                        std::string itag = instr[0];
                        if (itag == "pipeline")
                        {
                            if (instr.size() < 3 || !instr[1].is_number() || !instr[2].is_array())
                                throw std::runtime_error("invalid pipeline instruction");
                            int subjectIdx = instr[1];
                            const json& path = instr[2];
                            bool hasArgs = instr.size() >= 4;
                            json args = hasArgs ? instr[3] : json::array();

                            json resultVal;
                            if (subjectIdx < 0)
                            {
                                int capIndex = -subjectIdx - 1;
                                if (capIndex < 0 || capIndex >= (int)capturesVec.size())
                                    throw std::runtime_error("remap capture index out of range");
                                const Cap& c = capturesVec[capIndex];
                                if (c.isImport)
                                {
                                    // Import capture: treat as local dispatch on our main/target.
                                    json resolvedArgs = hasArgs ? eval(args) : json::array();
                                    if (!path.is_array() || path.empty() || !path[0].is_string())
                                        throw std::runtime_error("remap pipeline invalid method path");
                                    std::string method = path[0].get<std::string>();
                                    resultVal = dispatch(method, resolvedArgs);
                                }
                                else
                                {
                                    // Export capture: call back to the peer using client-call path.
                                    json resolvedArgs = hasArgs ? eval(args) : json::array();
                                    resultVal = callExport(c.id, path, resolvedArgs);
                                }
                            }
                            else
                            {
                                if (subjectIdx >= (int)variables.size())
                                    throw std::runtime_error("remap variable index out of range");
                                json subj = variables[subjectIdx];
                                resultVal = applyPathGet(subj, path);
                            }
                            variables.push_back(resultVal);
                        }
                        else if (itag == "value")
                        {
                            if (instr.size() != 2)
                                throw std::runtime_error("invalid value instruction");
                            variables.push_back(eval(instr[1]));
                        }
                        else if (itag == "get")
                        {
                            if (instr.size() != 3 || !instr[1].is_number() || !instr[2].is_array())
                                throw std::runtime_error("invalid get instruction");
                            int subjectIdx = instr[1];
                            const json& path = instr[2];
                            json resultVal;
                            if (subjectIdx < 0)
                            {
                                int capIndex = -subjectIdx - 1;
                                if (capIndex < 0 || capIndex >= (int)capturesVec.size())
                                    throw std::runtime_error("remap capture index out of range");
                                const Cap& c = capturesVec[capIndex];
                                if (c.isImport)
                                {
                                    resultVal = eval(json::array({"pipeline", c.id, path}));
                                }
                                else
                                {
                                    // Property get on export capture: not yet supported via client-call path.
                                    throw std::runtime_error("remap get on export capture not supported");
                                }
                            }
                            else
                            {
                                if (subjectIdx >= (int)variables.size())
                                    throw std::runtime_error("remap variable index out of range");
                                resultVal = applyPathGet(variables[subjectIdx], path);
                            }
                            variables.push_back(resultVal);
                        }
                        else if (itag == "array")
                        {
                            if (instr.size() != 2 || !instr[1].is_array())
                                throw std::runtime_error("invalid array instruction");
                            json out = json::array();
                            for (const auto& elem : instr[1])
                            {
                                if (elem.is_array() && elem.size() == 2 && elem[0].is_string() && elem[0] == "value")
                                    out.push_back(eval(elem[1]));
                                else
                                    out.push_back(eval(elem));
                            }
                            variables.push_back(out);
                        }
                        else if (itag == "object")
                        {
                            if (instr.size() != 2 || !instr[1].is_array())
                                throw std::runtime_error("invalid object instruction");
                            json out = json::object();
                            for (const auto& kv : instr[1])
                            {
                                if (!kv.is_array() || kv.size() != 2 || !kv[0].is_string())
                                    throw std::runtime_error("invalid object entry");
                                std::string key = kv[0].get<std::string>();
                                const auto& vexpr = kv[1];
                                if (vexpr.is_array() && vexpr.size() == 2 && vexpr[0].is_string() && vexpr[0] == "value")
                                    out[key] = eval(vexpr[1]);
                                else
                                    out[key] = eval(vexpr);
                            }
                            variables.push_back(out);
                        }
                        else if (itag == "remap")
                        {
                            variables.push_back(eval(instr));
                        }
                        else
                        {
                            throw std::runtime_error("unsupported remap instruction tag");
                        }
                    }

                    if (variables.empty())
                        return json();
                    else
                        return variables.back();
                }
                else if (tag == "value")
                {
                    if (v.size() != 2) throw std::runtime_error("invalid value expression");
                    return eval(v[1]);
                }
                else if (tag == "bigint")
                {
                    if (v.size() >= 2 && v[1].is_string()) return json{{"$bigint", v[1]}};
                }
                else if (tag == "date")
                {
                    if (v.size() >= 2 && (v[1].is_number() || v[1].is_number_integer())) return json{{"$date", v[1]}};
                }
                else if (tag == "bytes")
                {
                    if (v.size() >= 2 && v[1].is_string()) return json{{"$bytes", v[1]}};
                }
                else if (tag == "undefined")
                {
                    return json{{"$undefined", true}};
                }
                else if (tag == "error")
                {
                    if (v.size() >= 3 && v[1].is_string() && v[2].is_string())
                    {
                        json e = json{{"name", v[1].get<std::string>()}, {"message", v[2].get<std::string>()}};
                        if (v.size() >= 4 && v[3].is_string()) e["stack"] = v[3].get<std::string>();
                        return json{{"$error", e}};
                    }
                }
            }
            if (v.size() >= 2 && v[0].is_string() && v[0] == "pipeline" && v[1].is_number())
            {
                int exportId = v[1];
                json res;
                if (getResult(exportId, res))
                {
                    if (v.size() >= 3) res = traversePath(res, v[2]);
                    return res;
                }
                else
                {
                    std::string method; json args;
                    if (!getOperation(exportId, method, args)) throw std::runtime_error("Pipeline reference to non-existent export: " + std::to_string(exportId));
                    json resolvedArgs = eval(args);
                    json computed = dispatch(method, resolvedArgs);
                    cache(exportId, computed);
                    if (v.size() >= 3) computed = traversePath(computed, v[2]);
                    return computed;
                }
            }
            else
            {
                json out = json::array();
                for (const auto& e : v) out.push_back(eval(e));
                return out;
            }
        }
        else if (v.is_object())
        {
            json out = json::object();
            for (auto& [k, vv] : v.items()) out[k] = eval(vv);
            return out;
        }
        else
        {
            return v;
        }
    };

    return eval(value);
}

} // namespace serialize
} // namespace capnwebcpp
