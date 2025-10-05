enable_testing()

add_executable(capnwebcpp_tests_rpc
    test_rpc.cpp
)

target_link_libraries(capnwebcpp_tests_rpc PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_rpc COMMAND capnwebcpp_tests_rpc)

add_executable(capnwebcpp_tests_protocol
    test_protocol.cpp
)

target_link_libraries(capnwebcpp_tests_protocol PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_protocol COMMAND capnwebcpp_tests_protocol)
