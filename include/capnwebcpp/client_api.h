#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace capnwebcpp
{

using json = nlohmann::json;

// Transport for HTTP batch-style clients. Implementations take a batch of outbound
// frames (as newline-delimited messages) and return the peer's responses, in order.
class ClientBatchTransport
{
public:
    virtual ~ClientBatchTransport() = default;
    virtual std::vector<std::string> sendBatch(const std::vector<std::string>& lines) = 0;
};

// Adapter to a std::function for convenience.
class FuncBatchTransport : public ClientBatchTransport
{
public:
    explicit FuncBatchTransport(std::function<std::vector<std::string>(const std::vector<std::string>&)> fn)
        : fn(std::move(fn)) {}

    std::vector<std::string> sendBatch(const std::vector<std::string>& lines) override
    {
        return fn ? fn(lines) : std::vector<std::string>{};
    }

private:
    std::function<std::vector<std::string>(const std::vector<std::string>&)> fn;
};

// Minimal client for making RPC calls over an HTTP batch-like transport.
// This client constructs ["push", ["pipeline", 0, [method], args]] followed by
// a ["pull", importId] and returns the resolved value.
class RpcClient
{
public:
    explicit RpcClient(std::shared_ptr<ClientBatchTransport> transport)
        : transport(std::move(transport)) {}

    // Call a method on the remote main target.
    json callMethod(const std::string& method, const json& argsArray)
    {
        int importId = allocateImportId();
        json push = json::array({
            "push",
            json::array({ "pipeline", 0, json::array({ method }), argsArray.is_null() ? json::array() : argsArray })
        });
        json pull = json::array({ "pull", importId });
        std::vector<std::string> batch;
        batch.push_back(push.dump());
        batch.push_back(pull.dump());

        auto responses = transport->sendBatch(batch);
        return parseResultFor(importId, responses);
    }

    // Call a method on a previously-returned remote stub ({"$stub": exportId}).
    json callStubMethod(const json& stub, const std::string& method, const json& argsArray)
    {
        int exportId = getStubId(stub);
        if (exportId == 0) throw std::runtime_error("callStubMethod: not a stub");
        int importId = allocateImportId();
        json push = json::array({
            "push",
            json::array({ "pipeline", exportId, json::array({ method }), argsArray.is_null() ? json::array() : argsArray })
        });
        json pull = json::array({ "pull", importId });
        std::vector<std::string> batch;
        batch.push_back(push.dump());
        batch.push_back(pull.dump());
        auto responses = transport->sendBatch(batch);
        return parseResultFor(importId, responses);
    }

    // Get a property path from a previously-returned remote stub.
    json getStubProperty(const json& stub, const json& path)
    {
        int exportId = getStubId(stub);
        if (exportId == 0) throw std::runtime_error("getStubProperty: not a stub");
        int importId = allocateImportId();
        json push = json::array({ "push", json::array({ "pipeline", exportId, path }) });
        json pull = json::array({ "pull", importId });
        std::vector<std::string> batch;
        batch.push_back(push.dump());
        batch.push_back(pull.dump());
        auto responses = transport->sendBatch(batch);
        return parseResultFor(importId, responses);
    }

    // Construct a stub representation from an export id.
    static json makeStub(int exportId)
    {
        return json{ {"$stub", exportId} };
    }

    static bool isStub(const json& v)
    {
        return v.is_object() && v.contains("$stub") && v["$stub"].is_number_integer();
    }

    static int getStubId(const json& v)
    {
        return isStub(v) ? v["$stub"].get<int>() : 0;
    }

private:
    std::shared_ptr<ClientBatchTransport> transport;
    int nextImportId = 1;

    int allocateImportId()
    {
        return nextImportId++;
    }

    static json unwrapArrayIfNeeded(const json& v)
    {
        if (v.is_array() && v.size() == 1 && v[0].is_array())
            return v[0];
        return v;
    }

    static json decodeSpecial(const json& v)
    {
        if (v.is_array() && v.size() >= 1 && v[0].is_string())
        {
            std::string tag = v[0];
            if (tag == "export")
            {
                if (v.size() >= 2 && v[1].is_number_integer())
                    return makeStub(v[1].get<int>());
            }
            else if (tag == "promise")
            {
                // Represent as a promise stub placeholder for now.
                if (v.size() >= 2 && v[1].is_number_integer())
                    return json{ {"$promise_stub", v[1].get<int>()} };
            }
            else if (tag == "undefined")
            {
                return json{ {"$undefined", true} };
            }
            else if (tag == "bigint" && v.size() >= 2 && v[1].is_string())
            {
                return json{ {"$bigint", v[1]} };
            }
            else if (tag == "date" && v.size() >= 2 && v[1].is_number())
            {
                return json{ {"$date", v[1]} };
            }
            else if (tag == "bytes" && v.size() >= 2 && v[1].is_string())
            {
                return json{ {"$bytes", v[1]} };
            }
            else if (tag == "error" && v.size() >= 3 && v[1].is_string() && v[2].is_string())
            {
                return json{ {"$error", json{ {"name", v[1]}, {"message", v[2]} } } };
            }
        }
        return v;
    }

    json parseResultFor(int importId, const std::vector<std::string>& responses)
    {
        for (const auto& line : responses)
        {
            json m = json::parse(line, nullptr, false);
            if (!m.is_array() || m.size() < 2 || !m[0].is_string()) continue;
            std::string type = m[0];
            if ((type == "resolve" || type == "reject") && m[1].is_number_integer() && m[1] == importId)
            {
                if (type == "reject")
                {
                    if (m.size() >= 3 && m[2].is_array() && m[2].size() >= 3)
                    {
                        std::string name = m[2][1].is_string() ? m[2][1].get<std::string>() : std::string("Error");
                        std::string message = m[2][2].is_string() ? m[2][2].get<std::string>() : std::string("rejected");
                        throw std::runtime_error(name + ": " + message);
                    }
                    throw std::runtime_error("RPC rejected");
                }
                // resolve value
                json val = (m.size() >= 3) ? m[2] : json();
                val = unwrapArrayIfNeeded(val);
                val = decodeSpecial(val);
                return val;
            }
        }
        throw std::runtime_error("No resolution for importId");
    }
};

} // namespace capnwebcpp
