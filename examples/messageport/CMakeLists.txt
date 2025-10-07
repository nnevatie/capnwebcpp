cmake_minimum_required(VERSION 3.20)
project(messageport LANGUAGES CXX)

add_executable(${PROJECT_NAME} server.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

