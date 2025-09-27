#include <thread>

#include <capnwebcpp/capnwebcpp.hpp>

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
        std::thread thread([port]()
        {
            runRpcServer<HelloServer>(port, "/api");
        });
        runFileServer(port, path, "/static/");
    }
    return 0;
}
