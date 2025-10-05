enable_testing()

add_executable(capnwebcpp_tests
    test_rpc.cpp
)

target_link_libraries(capnwebcpp_tests PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests COMMAND capnwebcpp_tests)

