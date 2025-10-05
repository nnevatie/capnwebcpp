#pragma once

#include <string>
#include <vector>

#include "capnwebcpp/transport.h"

namespace capnwebcpp
{

// RpcTransport that collects all outgoing messages into a vector of strings.
class AccumTransport : public RpcTransport
{
public:
    explicit AccumTransport(std::vector<std::string>& out) : out(out) {}

    void send(const std::string& message) override
    {
        out.push_back(message);
    }

    void abort(const std::string& /*reason*/) override {}

private:
    std::vector<std::string>& out;
};

} // namespace capnwebcpp

