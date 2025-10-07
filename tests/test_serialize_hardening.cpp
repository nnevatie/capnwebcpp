#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include <capnwebcpp/serialize.h>

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

static bool testKeySanitization()
{
    auto getResult = [](int, json&) { return false; };
    auto getOperation = [](int, std::string&, json&) { return false; };
    auto dispatch = [](const std::string&, const json&) { return json(); };
    auto cache = [](int, const json&) {};

    json input = json::object({ {"__proto__", 1}, {"toJSON", 2}, {"x", 3} });
    json out = serialize::Evaluator::evaluateValue(input, getResult, getOperation, dispatch, cache);
    bool ok = true;
    ok &= require(out.is_object(), "sanitize: output is object");
    ok &= require(out.contains("x") && out["x"] == 3, "sanitize: keeps x");
    ok &= require(!out.contains("__proto__") && !out.contains("toJSON"), "sanitize: drops reserved keys");
    return ok;
}

static bool testInvalidPipelinePath()
{
    auto getResult = [](int, json&) { return true; };
    auto getOperation = [](int, std::string&, json&) { return false; };
    auto dispatch = [](const std::string&, const json&) { return json(); };
    auto cache = [](int, const json&) {};

    json expr = json::array({ "pipeline", 42, json::array({ "ok", true }) });
    bool threw = false;
    try
    {
        (void)serialize::Evaluator::evaluateValue(expr, getResult, getOperation, dispatch, cache);
    }
    catch (...)
    {
        threw = true;
    }
    return require(threw, "invalid path: throws");
}

static bool testDepthGuardDevaluate()
{
    // Build a deeply nested array: [[[[...]]]] depth 70
    json v = 0;
    for (int i = 0; i < 70; ++i) v = json::array({ v });
    bool threw = false;
    try
    {
        (void)serialize::devaluateForResult(v, [](bool, const json&){ return -1; });
    }
    catch (...)
    {
        threw = true;
    }
    return require(threw, "depth guard: devaluate throws");
}

int main()
{
    int failed = 0;
    failed += !testKeySanitization();
    failed += !testInvalidPipelinePath();
    failed += !testDepthGuardDevaluate();
    if (failed == 0)
    {
        std::cout << "All serialize hardening tests passed" << std::endl;
        return 0;
    }
    std::cerr << failed << " serialize hardening test(s) failed" << std::endl;
    return 1;
}

