cmake_minimum_required(VERSION 3.16)

project(helloworld LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

add_executable(${PROJECT_NAME} server.cpp)
target_link_libraries(${PROJECT_NAME} capnwebcpp)
