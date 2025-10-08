#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

namespace capnwebcpp
{

inline bool debugEnabled()
{
    static int enabled = -1;
    if (enabled == -1)
    {
        const char* v = std::getenv("CAPNWEBCPP_DEBUG");
        enabled = (v && *v) ? 1 : 0;
    }
    return enabled == 1;
}

inline void debugLog(const std::string& msg)
{
    if (debugEnabled())
    {
        std::cerr << "[capnwebcpp] " << msg << std::endl;
    }
}

} // namespace capnwebcpp

