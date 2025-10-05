#include "capnwebcpp/serialize.h"

namespace capnwebcpp
{
namespace serialize
{

json wrapArrayIfNeeded(const json& value)
{
    if (value.is_array())
        return json::array({ value });
    return value;
}

json makeError(const std::string& name, const std::string& message)
{
    return json::array({ "error", name, message });
}

json resolveFrame(int exportId, const json& value)
{
    return json::array({ "resolve", exportId, wrapArrayIfNeeded(value) });
}

json rejectFrame(int exportId, const json& errorTuple)
{
    return json::array({ "reject", exportId, errorTuple });
}

} // namespace serialize
} // namespace capnwebcpp

