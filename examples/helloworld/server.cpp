#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/endpoint.h>
#include <capnwebcpp/file_service.h>

using namespace capnwebcpp;

class HelloServer : public RpcTarget
{
public:
    HelloServer()
    {
        method("hello", [](const json& args)
        {
            return json("Hello, " + std::string(args[0]) + "!");
        });
    }
};

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        const auto port = 8000;
        const auto path = argv[1];

        uWS::App app;
        setupRpcEndpoint(app, "/api", std::make_shared<HelloServer>());
        setupFileEndpoint(app, "/static/", path);

        app.listen(port, [port](auto* token)
        {
            if (token)
                std::cout << "Listening on port " << port << std::endl;
            else
                std::cerr << "Failed to listen on port " << port << std::endl;
        })
        .run();
    }
    return 0;
}
