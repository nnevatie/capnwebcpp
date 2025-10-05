cmake_minimum_required(VERSION 3.16)

project(capnwebcpp VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(uwebsockets)
include(nlohmann_json)

# Build capnwebcpp library
add_subdirectory(src)

# Build examples
add_subdirectory(examples)

# Build tests
add_subdirectory(tests)
