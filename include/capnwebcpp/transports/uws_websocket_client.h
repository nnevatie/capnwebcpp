#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <App.h>

namespace capnwebcpp
{

// Minimal uWebSockets-based WebSocket client helper for persistent sessions.
// - Connects to a ws:// URL.
// - Provides send() to enqueue text frames on the uWS loop thread.
// - Invokes onMessage for each received text message.
class UwsWebSocketClient
{
public:
    UwsWebSocketClient() = default;
    ~UwsWebSocketClient() { close(); }

    // Connect to a WebSocket URL (ws://). Starts a background thread running the uWS loop.
    void connect(const std::string& url)
    {
        std::unique_lock<std::mutex> lock(mu);
        if (running) return;
        running = true;
        worker = std::thread([this, url]() { this->runLoop(url); });
        // Wait until open or error/close flagged
        openedCv.wait(lock, [this]() { return this->opened || this->closed; });
        if (!opened)
            throw std::runtime_error("WebSocket connect failed");
    }

    void setOnMessage(std::function<void(const std::string&)> cb)
    {
        std::lock_guard<std::mutex> lock(mu);
        onMessage = std::move(cb);
    }

    void send(const std::string& message)
    {
        std::function<void(const std::string&)> sendFnCopy;
        {
            std::lock_guard<std::mutex> lock(mu);
            sendFnCopy = scheduleSend;
        }
        if (!sendFnCopy)
            throw std::runtime_error("WebSocket not open");
        sendFnCopy(message);
    }

    void close()
    {
        std::thread t;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (!running) return;
            running = false;
            if (stopLoop) stopLoop();
            t = std::move(worker);
        }
        if (t.joinable()) t.join();
    }

private:
    std::mutex mu;
    std::thread worker;
    bool running = false;
    bool opened = false;
    bool closed = false;
    std::condition_variable openedCv;
    std::function<void(const std::string&)> onMessage;
    std::function<void(const std::string&)> scheduleSend;
    std::function<void()> stopLoop;

    void runLoop(const std::string& url)
    {
        // Per-socket data type
        struct PerSocketData { };

        // We'll allocate the app on the heap to control its lifetime explicitly.
        auto app = std::make_unique<uWS::App>();
        uWS::Loop* loop = uWS::Loop::get();

        {
            std::lock_guard<std::mutex> lock(mu);
            stopLoop = [loop]() { loop->defer([loop]() { loop->close(); }); };
        }

        app->connect(url,
        {
            .open = [this, loop](auto* ws)
            {
                {
                    std::lock_guard<std::mutex> lock(mu);
                    opened = true;
                    // Arrange to schedule sends onto the loop thread safely.
                    scheduleSend = [loop, ws](const std::string& m)
                    {
                        // Copy message for deferred send
                        loop->defer([ws, m]() { ws->send(m, uWS::TEXT); });
                    };
                }
                openedCv.notify_all();
            },
            .message = [this](auto* /*ws*/, std::string_view message, uWS::OpCode)
            {
                std::function<void(const std::string&)> cb;
                {
                    std::lock_guard<std::mutex> lock(mu);
                    cb = onMessage;
                }
                if (cb) cb(std::string(message));
            },
            .close = [this](auto* /*ws*/, int /*code*/, std::string_view /*msg*/)
            {
                {
                    std::lock_guard<std::mutex> lock(mu);
                    closed = true;
                }
                openedCv.notify_all();
            }
        });

        app->run();
        // clean up
        {
            std::lock_guard<std::mutex> lock(mu);
            scheduleSend = nullptr;
        }
    }
};

} // namespace capnwebcpp
