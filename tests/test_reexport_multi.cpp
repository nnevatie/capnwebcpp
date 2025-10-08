#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/rpc_target.h>
#include <capnwebcpp/rpc_session.h>
#include <capnwebcpp/export_target.h>

using json = nlohmann::json;
using namespace capnwebcpp;

static bool require(bool cond, const std::string& msg)
{
    if (!cond)
    {
        std::cerr << "TEST FAILED: " << msg << std::endl;
        return false;
    }
    return true;
}

struct SubTarget : public RpcTarget
{
    SubTarget(const std::string& name)
    {
        method("name", [name](const json&){ return json(name); });
    }
};

struct MainTarget : public RpcTarget
{
    MainTarget(RpcSessionData* data)
        : data(data), a(std::make_shared<SubTarget>("A")), b(std::make_shared<SubTarget>("B"))
    {
        method("getA", [this](const json&){ return exportTarget(this->data, this->a); });
        method("getB", [this](const json&){ return exportTarget(this->data, this->b); });
    }
    RpcSessionData* data;
    std::shared_ptr<SubTarget> a;
    std::shared_ptr<SubTarget> b;
};

static json parse(const std::string& s) { return json::parse(s); }

static bool testMultiTargetReexportReuseIds()
{
    RpcSession session(nullptr);
    RpcSessionData data; data.target = std::make_shared<MainTarget>(&data);

    // Export A twice; expect same negId
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"getA"})})}).dump());
    json r1 = parse(session.handleMessage(&data, json::array({"pull", 1}).dump()));
    int a1 = r1[2][1];
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"getA"})})}).dump());
    json r2 = parse(session.handleMessage(&data, json::array({"pull", 2}).dump()));
    int a2 = r2[2][1];

    // Export B twice; expect same negId but different from A
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"getB"})})}).dump());
    json r3 = parse(session.handleMessage(&data, json::array({"pull", 3}).dump()));
    int b1 = r3[2][1];
    session.handleMessage(&data, json::array({"push", json::array({"pipeline", 0, json::array({"getB"})})}).dump());
    json r4 = parse(session.handleMessage(&data, json::array({"pull", 4}).dump()));
    int b2 = r4[2][1];

    bool ok = true;
    ok &= require(a1 < 0 && b1 < 0, "ids are negative");
    ok &= require(a1 == a2, "A reuse");
    ok &= require(b1 == b2, "B reuse");
    ok &= require(a1 != b1, "A vs B distinct ids");
    return ok;
}

int main()
{
    int failed = 0;
    failed += !testMultiTargetReexportReuseIds();
    if (failed == 0)
    {
        std::cout << "All multi-target re-export tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " multi-target re-export test(s) failed" << std::endl;
    return 1;
}
