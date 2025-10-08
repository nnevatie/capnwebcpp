#pragma once

#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "capnwebcpp/transports/uws_websocket_client.h"

namespace capnwebcpp
{

using json = nlohmann::json;

// Persistent WebSocket RPC client using uWebSockets.
class RpcWsClient
{
public:
    explicit RpcWsClient(const std::string& url)
        : url(url), ws(std::make_shared<UwsWebSocketClient>())
    {
        ws->setOnMessage([this](const std::string& message) { this->handleMessage(message); });
        ws->connect(url);
    }

    ~RpcWsClient()
    {
        try { close(); } catch (...) {}
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(mu);
        if (closed) return;
        closed = true;
        ws->close();
    }

    // Call a method on main target; blocks until resolution and returns decoded JSON.
    json callMethod(const std::string& method, const json& argsArray)
    {
        int importId = allocateImportId();
        json push = json::array({
            "push",
            json::array({ "pipeline", 0, json::array({ method }), argsArray.is_null() ? json::array() : argsArray })
        });
        send(push.dump());
        send(json::array({ "pull", importId }).dump());

        json value = awaitResolution(importId);
        // Release our import to avoid server leaks.
        send(json::array({ "release", importId, 1 }).dump());
        return value;
    }

    // Call a method on a stub returned by a previous call.
    json callStubMethod(const json& stub, const std::string& method, const json& argsArray)
    {
        int exportId = getStubId(stub);
        if (exportId == 0) throw std::runtime_error("callStubMethod: not a stub");
        int importId = allocateImportId();
        json push = json::array({
            "push",
            json::array({ "pipeline", exportId, json::array({ method }), argsArray.is_null() ? json::array() : argsArray })
        });
        send(push.dump());
        send(json::array({ "pull", importId }).dump());
        json value = awaitResolution(importId);
        send(json::array({ "release", importId, 1 }).dump());
        return value;
    }

    // Get a property from a stub (path is a JSON array of string/number parts).
    json getStubProperty(const json& stub, const json& path)
    {
        int exportId = getStubId(stub);
        if (exportId == 0) throw std::runtime_error("getStubProperty: not a stub");
        int importId = allocateImportId();
        send(json::array({ "push", json::array({ "pipeline", exportId, path }) }).dump());
        send(json::array({ "pull", importId }).dump());
        json value = awaitResolution(importId);
        send(json::array({ "release", importId, 1 }).dump());
        return value;
    }

    // Helpers for stub markers.
    static json makeStub(int exportId) { return json{ {"$stub", exportId} }; }
    static bool isStub(const json& v) { return v.is_object() && v.contains("$stub") && v["$stub"].is_number_integer(); }
    static int getStubId(const json& v) { return isStub(v) ? v["$stub"].get<int>() : 0; }

private:
    std::string url;
    std::shared_ptr<UwsWebSocketClient> ws;

    std::mutex mu;
    bool closed = false;
    int nextImportId = 1;
    struct Pending
    {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        json value;
        std::string error;
    };
    std::map<int, std::shared_ptr<Pending>> pending;

    int allocateImportId()
    {
        std::lock_guard<std::mutex> lock(mu);
        int id = nextImportId++;
        pending[id] = std::make_shared<Pending>();
        return id;
    }

    void send(const std::string& m)
    {
        ws->send(m);
    }

    static json unwrapArrayIfNeeded(const json& v)
    {
        if (v.is_array() && v.size() == 1 && v[0].is_array()) return v[0];
        return v;
    }

    static json decodeSpecial(const json& v)
    {
        if (v.is_array() && v.size() >= 1 && v[0].is_string())
        {
            std::string tag = v[0];
            if (tag == "export")
            {
                if (v.size() >= 2 && v[1].is_number_integer()) return makeStub(v[1].get<int>());
            }
            else if (tag == "promise")
            {
                if (v.size() >= 2 && v[1].is_number_integer()) return json{ {"$promise_stub", v[1].get<int>()} };
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

    json awaitResolution(int importId)
    {
        std::shared_ptr<Pending> p;
        {
            std::lock_guard<std::mutex> lock(mu);
            auto it = pending.find(importId);
            if (it == pending.end()) throw std::runtime_error("awaitResolution: unknown id");
            p = it->second;
        }
        std::unique_lock<std::mutex> plock(p->m);
        p->cv.wait(plock, [&]() { return p->done; });
        if (!p->error.empty()) throw std::runtime_error(p->error);
        json v = p->value;
        {
            std::lock_guard<std::mutex> lock(mu);
            pending.erase(importId);
        }
        return v;
    }

    void handleMessage(const std::string& message)
    {
        json m = json::parse(message, nullptr, false);
        if (!m.is_array() || m.empty() || !m[0].is_string()) return;
        std::string tag = m[0];
        if ((tag == "resolve" || tag == "reject") && m.size() >= 3 && m[1].is_number_integer())
        {
            int importId = m[1].get<int>();
            std::shared_ptr<Pending> p;
            {
                std::lock_guard<std::mutex> lock(mu);
                auto it = pending.find(importId);
                if (it == pending.end()) return;
                p = it->second;
            }
            if (tag == "reject")
            {
                std::string name = "Error";
                std::string messageText = "rejected";
                if (m[2].is_array() && m[2].size() >= 3)
                {
                    if (m[2][1].is_string()) name = m[2][1].get<std::string>();
                    if (m[2][2].is_string()) messageText = m[2][2].get<std::string>();
                }
                {
                    std::lock_guard<std::mutex> lk(p->m);
                    p->error = name + ": " + messageText;
                    p->done = true;
                }
                p->cv.notify_all();
                return;
            }
            // resolve
            json val = m[2];
            val = unwrapArrayIfNeeded(val);
            val = decodeSpecial(val);
            {
                std::lock_guard<std::mutex> lk(p->m);
                p->value = val;
                p->done = true;
            }
            p->cv.notify_all();
        }
        else if (tag == "abort")
        {
            std::lock_guard<std::mutex> lock(mu);
            for (auto& kv : pending)
            {
                {
                    std::lock_guard<std::mutex> lk(kv.second->m);
                    kv.second->error = "aborted";
                    kv.second->done = true;
                }
                kv.second->cv.notify_all();
            }
            pending.clear();
        }
    }
};

} // namespace capnwebcpp
