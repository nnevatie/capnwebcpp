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
    const int port = argc > 1 ? std::atoi(argv[1]) : 8000;
    runRpcServer<HelloServer>(port, "/api");
    return 0;
}
