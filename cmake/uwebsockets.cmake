# cmake/uwebsockets.cmake
# Creates an INTERFACE target: uwebsockets::uwebsockets
# - Fetches uWebSockets (headers) and uSockets (C sources)
# - Automatically builds libuv from source on Windows
# - Optional TLS via OpenSSL (UWS_WITH_SSL=ON)
#
# Usage:
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
#   include(uwebsockets)
#   target_link_libraries(your_target PRIVATE uwebsockets::uwebsockets)

include_guard(GLOBAL)

# Prefer modern FetchContent behavior (CMake ≥ 3.30)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 NEW)
endif()

# Ensure C language is available
enable_language(C OPTIONAL)
if(NOT CMAKE_C_COMPILER)
  message(FATAL_ERROR "C compiler required to build uSockets")
endif()

# Configuration options
set(UWS_GIT_TAG v20.74.0 CACHE STRING "uWebSockets version tag")
set(USOCKETS_GIT_TAG v0.8.8 CACHE STRING "uSockets version tag")
set(UWS_WITH_SSL ON CACHE BOOL "Enable SSL/TLS support")
set(LIBUV_GIT_TAG v1.48.0 CACHE STRING "libuv version tag (for Windows)")

include(FetchContent)
find_package(Threads REQUIRED)

# ========== uSockets ==========
if(NOT TARGET uSockets)
  # Fetch uSockets
  FetchContent_Declare(
    uSockets
    GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
    GIT_TAG        ${USOCKETS_GIT_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
  )
  FetchContent_MakeAvailable(uSockets)

  # Get source directory
  set(_USOCKETS_SRC "${CMAKE_BINARY_DIR}/_deps/uSockets-src")
  if(DEFINED uSockets_SOURCE_DIR AND NOT uSockets_SOURCE_DIR STREQUAL "")
    set(_USOCKETS_SRC "${uSockets_SOURCE_DIR}")
  endif()

  if(NOT TARGET uSockets)
    # Build uSockets from sources
    message(STATUS "Building uSockets from sources")

    # Collect source files
    file(GLOB_RECURSE USOCKETS_ALL_C
      "${_USOCKETS_SRC}/src/*.c"
      "${_USOCKETS_SRC}/src/*/*.c"
    )
    set(USOCKETS_C_SOURCES ${USOCKETS_ALL_C})

    # Exclude unnecessary components
    list(FILTER USOCKETS_C_SOURCES EXCLUDE REGEX ".*/src/eventing/.*\\.c$")
    list(FILTER USOCKETS_C_SOURCES EXCLUDE REGEX ".*/src/quic/.*\\.c$")
    list(FILTER USOCKETS_C_SOURCES EXCLUDE REGEX ".*/src/crypto/.*\\.c$")

    # Platform-specific adjustments
    if(WIN32)
      list(FILTER USOCKETS_C_SOURCES EXCLUDE REGEX ".*/src/bsd\\.c$")
      # On Windows, we always use libuv
      file(GLOB _UWS_LIBUV_C "${_USOCKETS_SRC}/src/eventing/libuv*.c")
      list(APPEND USOCKETS_C_SOURCES ${_UWS_LIBUV_C})
    else()
      # On Unix systems, add appropriate backend
      if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        file(GLOB _UWS_KQUEUE_C "${_USOCKETS_SRC}/src/eventing/epoll_kqueue.c")
        list(APPEND USOCKETS_C_SOURCES ${_UWS_KQUEUE_C})
      else()
        file(GLOB _UWS_EPOLL_C "${_USOCKETS_SRC}/src/eventing/epoll_kqueue.c")
        list(APPEND USOCKETS_C_SOURCES ${_UWS_EPOLL_C})
      endif()
    endif()

    add_library(uSockets STATIC ${USOCKETS_C_SOURCES})
    add_library(uSockets::uSockets ALIAS uSockets)
    set_target_properties(uSockets PROPERTIES LINKER_LANGUAGE C)
    target_include_directories(uSockets PUBLIC "${_USOCKETS_SRC}/src")
    target_link_libraries(uSockets PUBLIC Threads::Threads)

    # Platform-specific configuration
    if(WIN32)
      # Windows requires libuv
      target_compile_definitions(uSockets PUBLIC LIBUS_USE_LIBUV WIN32_LEAN_AND_MEAN NOMINMAX)
      target_link_libraries(uSockets PUBLIC ws2_32)

      # Build libuv from source
      message(STATUS "Building libuv from source for Windows")
      FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG        ${LIBUV_GIT_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
      )

      set(LIBUV_BUILD_SHARED OFF CACHE INTERNAL "")
      set(BUILD_TESTING OFF CACHE INTERNAL "")
      set(BUILD_BENCHMARKS OFF CACHE INTERNAL "")

      FetchContent_MakeAvailable(libuv)

      if(TARGET uv_a)
        target_link_libraries(uSockets PUBLIC uv_a)
      elseif(TARGET uv)
        target_link_libraries(uSockets PUBLIC uv)
      endif()

      if(DEFINED libuv_SOURCE_DIR)
        target_include_directories(uSockets PUBLIC "${libuv_SOURCE_DIR}/include")
      else()
        target_include_directories(uSockets PUBLIC "${CMAKE_BINARY_DIR}/_deps/libuv-src/include")
      endif()
    else()
      # Unix systems
      target_compile_definitions(uSockets PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
      if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        target_compile_definitions(uSockets PUBLIC LIBUS_USE_KQUEUE)
      else()
        target_compile_definitions(uSockets PUBLIC LIBUS_USE_EPOLL)
      endif()
    endif()

    # Optional SSL/TLS support
    if(UWS_WITH_SSL)
      find_package(OpenSSL QUIET)
      if(OpenSSL_FOUND)
        target_link_libraries(uSockets PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(uSockets PUBLIC WITH_OPENSSL=1)
      else()
        message(STATUS "OpenSSL not found - building without TLS support")
      endif()
    endif()
  endif()
endif()

# ========== uWebSockets (headers) ==========
FetchContent_Declare(
  uWebSockets
  GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
  GIT_TAG        ${UWS_GIT_TAG}
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(uWebSockets)

# Find uWebSockets headers
set(_UWS_SRC "${CMAKE_BINARY_DIR}/_deps/uWebSockets-src")
if(DEFINED uWebSockets_SOURCE_DIR AND NOT uWebSockets_SOURCE_DIR STREQUAL "")
  set(_UWS_SRC "${uWebSockets_SOURCE_DIR}")
endif()

# Check for headers location
set(_UWS_INCLUDE_DIR "")
foreach(dir "${_UWS_SRC}/src" "${_UWS_SRC}" "${_UWS_SRC}/include")
  if(EXISTS "${dir}/App.h")
    set(_UWS_INCLUDE_DIR "${dir}")
    break()
  endif()
endforeach()

if(_UWS_INCLUDE_DIR STREQUAL "")
  message(FATAL_ERROR "uWebSockets headers not found. Check UWS_GIT_TAG=${UWS_GIT_TAG}")
endif()

# Create interface target
if(NOT TARGET uwebsockets::uwebsockets)
  add_library(uwebsockets::uwebsockets INTERFACE IMPORTED GLOBAL)
endif()

set_property(TARGET uwebsockets::uwebsockets PROPERTY
  INTERFACE_INCLUDE_DIRECTORIES "${_UWS_INCLUDE_DIR}"
)
set_property(TARGET uwebsockets::uwebsockets PROPERTY
  INTERFACE_LINK_LIBRARIES "uSockets;Threads::Threads"
)

message(STATUS "uWebSockets configured successfully")
