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

add_executable(capnwebcpp_tests_batch
    test_batch.cpp
)

target_link_libraries(capnwebcpp_tests_batch PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_batch COMMAND capnwebcpp_tests_batch)

add_executable(capnwebcpp_tests_abort
    test_abort.cpp
)

target_link_libraries(capnwebcpp_tests_abort PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_abort COMMAND capnwebcpp_tests_abort)

add_executable(capnwebcpp_tests_drain_stats
    test_drain_stats.cpp
)

target_link_libraries(capnwebcpp_tests_drain_stats PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_drain_stats COMMAND capnwebcpp_tests_drain_stats)

add_executable(capnwebcpp_tests_remap_captures
    test_remap_captures.cpp
)

target_link_libraries(capnwebcpp_tests_remap_captures PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_remap_captures COMMAND capnwebcpp_tests_remap_captures)

add_executable(capnwebcpp_tests_client_call
    test_client_call.cpp
)

target_link_libraries(capnwebcpp_tests_client_call PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_client_call COMMAND capnwebcpp_tests_client_call)

add_executable(capnwebcpp_tests_batch_export_capture_get
    test_batch_export_capture_get.cpp
)

target_link_libraries(capnwebcpp_tests_batch_export_capture_get PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_batch_export_capture_get COMMAND capnwebcpp_tests_batch_export_capture_get)
