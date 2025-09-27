cmake_minimum_required(VERSION 3.16)

project(capnwebcpp VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(uwebsockets)

add_library(capnwebcpp src/capnwebcpp.cpp)
target_link_libraries(capnwebcpp PUBLIC uwebsockets::uwebsockets)

add_subdirectory(examples)
