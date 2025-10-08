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

add_executable(capnwebcpp_tests_reexport
    test_reexport.cpp
)

target_link_libraries(capnwebcpp_tests_reexport PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_reexport COMMAND capnwebcpp_tests_reexport)

add_executable(capnwebcpp_tests_reexport_aggregate_release
    test_reexport_aggregate_release.cpp
)

target_link_libraries(capnwebcpp_tests_reexport_aggregate_release PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_reexport_aggregate_release COMMAND capnwebcpp_tests_reexport_aggregate_release)

add_executable(capnwebcpp_tests_serialize_hardening
    test_serialize_hardening.cpp
)

target_link_libraries(capnwebcpp_tests_serialize_hardening PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_serialize_hardening COMMAND capnwebcpp_tests_serialize_hardening)

add_executable(capnwebcpp_tests_transport_abort
    test_transport_abort.cpp
)

target_link_libraries(capnwebcpp_tests_transport_abort PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_transport_abort COMMAND capnwebcpp_tests_transport_abort)

add_executable(capnwebcpp_tests_cpp_client
    test_cpp_client.cpp
)

target_link_libraries(capnwebcpp_tests_cpp_client PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_cpp_client COMMAND capnwebcpp_tests_cpp_client)

add_executable(capnwebcpp_tests_server_call_api
    test_server_call_api.cpp
)

target_link_libraries(capnwebcpp_tests_server_call_api PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_server_call_api COMMAND capnwebcpp_tests_server_call_api)

add_executable(capnwebcpp_tests_redaction
    test_redaction.cpp
)

target_link_libraries(capnwebcpp_tests_redaction PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_redaction COMMAND capnwebcpp_tests_redaction)

add_executable(capnwebcpp_tests_import_stub
    test_import_stub.cpp
)

target_link_libraries(capnwebcpp_tests_import_stub PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_import_stub COMMAND capnwebcpp_tests_import_stub)

add_executable(capnwebcpp_tests_import_promise
    test_import_promise.cpp
)

target_link_libraries(capnwebcpp_tests_import_promise PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_import_promise COMMAND capnwebcpp_tests_import_promise)

add_executable(capnwebcpp_tests_reexport_multi
    test_reexport_multi.cpp
)

target_link_libraries(capnwebcpp_tests_reexport_multi PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_reexport_multi COMMAND capnwebcpp_tests_reexport_multi)

add_executable(capnwebcpp_tests_remap_promise_capture
    test_remap_promise_capture.cpp
)

target_link_libraries(capnwebcpp_tests_remap_promise_capture PRIVATE
    capnwebcpp
    nlohmann_json::nlohmann_json
)

add_test(NAME capnwebcpp_tests_remap_promise_capture COMMAND capnwebcpp_tests_remap_promise_capture)
