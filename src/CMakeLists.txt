# Cap'n Web RPC C++ Library

# Glob all cpp files in src directory
file(GLOB CAPNWEBCPP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Create the library
add_library(capnwebcpp STATIC
    ${CAPNWEBCPP_SOURCES}
)

# Set include directories
target_include_directories(capnwebcpp PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Link dependencies - use the proper target names
target_link_libraries(capnwebcpp PUBLIC
    uwebsockets::uwebsockets
    nlohmann_json::nlohmann_json
)

# Set C++ standard
target_compile_features(capnwebcpp PUBLIC cxx_std_20)