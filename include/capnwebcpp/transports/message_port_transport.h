#pragma once

#include <functional>
#include <memory>
#include <string>

#include "capnwebcpp/transport.h"

namespace capnwebcpp
{

class MessagePort;

// Simple in-process MessagePort. postMessage() delivers to the peer's handler.
class MessagePort
{
public:
    using Handler = std::function<void(const std::string&)>;

    void setPeer(MessagePort* p)
    {
        peer = p;
    }

    void setHandler(Handler h)
    {
        handler = std::move(h);
    }

    void postMessage(const std::string& message)
    {
        if (peer && peer->handler)
        {
            peer->handler(message);
        }
    }

private:
    MessagePort* peer = nullptr;
    Handler handler;
};

// A pair of connected MessagePorts.
struct MessageChannel
{
    MessageChannel()
    {
        port1.setPeer(&port2);
        port2.setPeer(&port1);
    }

    MessagePort port1;
    MessagePort port2;
};

// RpcTransport adapter that sends via a MessagePort.
class MessagePortTransport : public RpcTransport
{
public:
    explicit MessagePortTransport(MessagePort* port) : port(port) {}

    void send(const std::string& message) override
    {
        if (port) port->postMessage(message);
    }

    void abort(const std::string& /*reason*/) override
    {
        // No special state to clear for in-process transport.
    }

private:
    MessagePort* port;
};

} // namespace capnwebcpp

