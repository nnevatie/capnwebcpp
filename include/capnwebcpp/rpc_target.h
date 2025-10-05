#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace capnwebcpp
{

using json = nlohmann::json;

// RpcTarget is the base class for server implementations.
class RpcTarget
{
public:
    virtual ~RpcTarget() = default;

    // Dispatch a method call to a registered handler.
    virtual json dispatch(const std::string& method, const json& args)
    {
        auto it = methods.find(method);
        if (it != methods.end())
            return it->second(args);

        throw std::runtime_error("Method not found: " + method);
    }

protected:
    // Register a method handler.
    void method(const std::string& name, std::function<json(const json&)> handler)
    {
        methods[name] = handler;
    }

private:
    std::unordered_map<std::string, std::function<json(const json&)>> methods;
};

} // namespace capnwebcpp
