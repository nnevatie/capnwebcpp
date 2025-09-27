#include <iostream>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <App.h>

using json = nlohmann::json;

struct PerSocketData {
    std::unordered_map<int, json> pendingResults;
    int nextExportId = 1;  // Start from 1, not 0
};

int main(int argc, char** argv)
{
    const auto port = argc > 1 ? std::atoi(argv[1]) : 8000;

    uWS::App().get("/api", [](auto* res, auto* req)
    {
        res->end("Hello World!");
    })
    .ws<PerSocketData>("/api", {
        .compression = uWS::DISABLED,
        .maxPayloadLength = 16 * 1024 * 1024,
        .idleTimeout = 120,
        .maxBackpressure = 1 * 1024 * 1024,
        .closeOnBackpressureLimit = false,
        .resetIdleTimeoutOnSend = false,
        .sendPingsAutomatically = true,

        .upgrade = nullptr,
        .open = [](auto* ws) {
            std::cout << "WebSocket connection opened" << std::endl;
            ws->getUserData()->nextExportId = 1;  // Reset to 1 for new connections
            ws->getUserData()->pendingResults.clear();
        },
        .message = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            try {
                auto msg = json::parse(message);
                std::cout << "Received message: " << msg.dump() << std::endl;

                if (!msg.is_array() || msg.empty()) {
                    std::cerr << "Invalid message format" << std::endl;
                    return;
                }

                std::string messageType = msg[0];
                auto* userData = ws->getUserData();

                if (messageType == "push") {
                    // Handle pipeline push: ["push", ["pipeline", pipelineId, [method], [args...]]]
                    if (msg.size() >= 2 && msg[1].is_array()) {
                        auto& pushData = msg[1];

                        // The push creates a new export on the server side
                        // We assign it the next available ID (starting from 1)
                        int exportId = userData->nextExportId++;

                        if (pushData[0] == "pipeline" && pushData.size() >= 3) {
                            int importId = pushData[1];
                            auto methodArray = pushData[2];
                            auto argsArray = pushData.size() >= 4 ? pushData[3] : json::array();

                            if (methodArray.is_array() && !methodArray.empty()) {
                                std::string method = methodArray[0];

                                if (method == "hello") {
                                    std::string name = "World";
                                    if (argsArray.is_array() && !argsArray.empty()) {
                                        name = argsArray[0];
                                    }

                                    // Store the result for when the client pulls it
                                    userData->pendingResults[exportId] = "Hello, " + name + "!";
                                }
                            }
                        }
                    }
                } else if (messageType == "pull") {
                    // Handle pull request: ["pull", exportId]
                    if (msg.size() >= 2 && msg[1].is_number()) {
                        int exportId = msg[1];

                        if (userData->pendingResults.find(exportId) != userData->pendingResults.end()) {
                            // Send the result using "resolve" message
                            json resolveResponse = json::array({"resolve", exportId, userData->pendingResults[exportId]});
                            ws->send(resolveResponse.dump(), uWS::TEXT);

                            // Clean up
                            userData->pendingResults.erase(exportId);
                        } else {
                            // Export ID not found - send an error
                            json errorResponse = json::array({"reject", exportId, json::array({"error", "ExportNotFound", "Export ID not found"})});
                            ws->send(errorResponse.dump(), uWS::TEXT);
                        }
                    }
                } else if (messageType == "abort") {
                    // Handle abort - typically just log it
                    std::cerr << "Abort received: " << msg.dump() << std::endl;
                }

            } catch (const std::exception& e) {
                std::cerr << "Error processing message: " << e.what() << std::endl;
            }
        },
        .dropped = [](auto* ws, std::string_view message, uWS::OpCode opCode) {
            std::cerr << "Message dropped!" << std::endl;
        },
        .drain = [](auto* ws) {
            // Called when ws is able to send more data
        },
        .ping = [](auto* ws, std::string_view) {
            // Handle ping
        },
        .pong = [](auto* ws, std::string_view) {
            // Handle pong
        },
        .close = [](auto* ws, int code, std::string_view message) {
            std::cout << "WebSocket connection closed with code " << code << std::endl;
        }
    })
    .listen(port, [=](auto* token)
    {
        if (token)
            std::cout << "Listening on port " << port << std::endl;
    })
    .run();
    return 0;
}
