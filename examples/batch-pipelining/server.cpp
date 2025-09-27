#include <map>
#include <vector>

#include <capnwebcpp/rpc_service.hpp>

using namespace capnwebcpp;

class UserServer : public RpcTarget
{
public:
    UserServer()
    {
        // Initialize in-memory data
        initializeData();

        // Register methods
        method("authenticate", [this](const json& args) -> json
        {
            // Extract session token from arguments
            std::string sessionToken;
            if (args.is_array() && !args.empty() && args[0].is_string())
                sessionToken = args[0];
            else if (args.is_string())
                sessionToken = args;
            else
                throw std::runtime_error("Invalid session token");

            // Look up user by session token
            auto it = users.find(sessionToken);
            if (it == users.end())
                throw std::runtime_error("Invalid session");

            return it->second;
        });

        method("getUserProfile", [this](const json& args) -> json
        {
            // Extract user ID from arguments
            std::string userId;
            if (args.is_array() && !args.empty() && args[0].is_string())
                userId = args[0];
            else if (args.is_string())
                userId = args;
            else
                throw std::runtime_error("Invalid user ID");

            // Look up profile by user ID
            auto it = profiles.find(userId);
            if (it == profiles.end())
                throw std::runtime_error("No such user");

            return it->second;
        });

        method("getNotifications", [this](const json& args) -> json
        {
            // Extract user ID from arguments
            std::string userId;
            if (args.is_array() && !args.empty() && args[0].is_string())
                userId = args[0];
            else if (args.is_string())
                userId = args;
            else
                throw std::runtime_error("Invalid user ID");

            // Look up notifications by user ID
            auto it = notifications.find(userId);
            if (it == notifications.end())
                return json::array();

            return it->second;
        });
    }

private:
    // In-memory data stores
    std::map<std::string, json> users;
    std::map<std::string, json> profiles;
    std::map<std::string, json> notifications;

    void initializeData()
    {
        // Initialize users (session token -> user object)
        users["cookie-123"] = json{
            {"id", "u_1"},
            {"name", "Ada Lovelace"}
        };
        users["cookie-456"] = json{
            {"id", "u_2"},
            {"name", "Alan Turing"}
        };

        // Initialize profiles (user ID -> profile object)
        profiles["u_1"] = json{
            {"id", "u_1"},
            {"bio", "Mathematician & first programmer"}
        };
        profiles["u_2"] = json{
            {"id", "u_2"},
            {"bio", "Mathematician & computer science pioneer"}
        };

        // Initialize notifications (user ID -> array of notifications)
        notifications["u_1"] = json::array({
            "Welcome to jsrpc!",
            "You have 2 new followers"
        });
        notifications["u_2"] = json::array({
            "New feature: pipelining!",
            "Security tips for your account"
        });
    }
};

int main(int argc, char** argv)
{
    /*
    const int port = argc > 1 ? std::atoi(argv[1]) : 8000;
    runRpcServer<UserServer>(port, "/rpc");
    */
    return 0;
}
