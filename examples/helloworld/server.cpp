#include <App.h>

int main(int argc, char** argv)
{
    const auto port = argc > 1 ? std::atoi(argv[1]) : 8000;
    uWS::App().get("/*", [](auto* res, auto* req)
    {
        res->end("Hello World!");
    })
    .listen(port, [=](auto* token)
    {
        if (token)
            std::cout << "Listening on port " << port << std::endl;
    })
    .run();
    return 0;
}
