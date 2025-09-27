# cmake/nlohmann_json.cmake
# Creates an INTERFACE target: nlohmann_json::nlohmann_json
# - Fetches nlohmann/json (header-only library)
#
# Usage:
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
#   include(nlohmann_json)
#   target_link_libraries(your_target PRIVATE nlohmann_json::nlohmann_json)

include_guard(GLOBAL)

# Prefer modern FetchContent behavior (CMake ≥ 3.30)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 NEW)
endif()

# Configuration options
set(NLOHMANN_JSON_GIT_TAG v3.11.3 CACHE STRING "nlohmann/json version tag")

include(FetchContent)

# ========== nlohmann/json ==========
if(NOT TARGET nlohmann_json::nlohmann_json)
  # Fetch nlohmann/json
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        ${NLOHMANN_JSON_GIT_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
  )

  # Disable tests and other unnecessary components
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  set(JSON_Install OFF CACHE INTERNAL "")
  set(JSON_MultipleHeaders OFF CACHE INTERNAL "")
  set(JSON_ImplicitConversions ON CACHE INTERNAL "")

  FetchContent_MakeAvailable(nlohmann_json)

  # The FetchContent should create the target nlohmann_json::nlohmann_json
  # If not, we'll create it manually
  if(NOT TARGET nlohmann_json::nlohmann_json)
    # Get source directory
    set(_NLOHMANN_JSON_SRC "${CMAKE_BINARY_DIR}/_deps/nlohmann_json-src")
    if(DEFINED nlohmann_json_SOURCE_DIR AND NOT nlohmann_json_SOURCE_DIR STREQUAL "")
      set(_NLOHMANN_JSON_SRC "${nlohmann_json_SOURCE_DIR}")
    endif()

    # Check for header location
    set(_NLOHMANN_JSON_INCLUDE_DIR "")
    foreach(dir "${_NLOHMANN_JSON_SRC}/include" "${_NLOHMANN_JSON_SRC}/single_include" "${_NLOHMANN_JSON_SRC}")
      if(EXISTS "${dir}/nlohmann/json.hpp")
        set(_NLOHMANN_JSON_INCLUDE_DIR "${dir}")
        break()
      endif()
    endforeach()

    if(_NLOHMANN_JSON_INCLUDE_DIR STREQUAL "")
      message(FATAL_ERROR "nlohmann/json headers not found. Check NLOHMANN_JSON_GIT_TAG=${NLOHMANN_JSON_GIT_TAG}")
    endif()

    # Create interface target
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED GLOBAL)
    set_property(TARGET nlohmann_json::nlohmann_json PROPERTY
      INTERFACE_INCLUDE_DIRECTORIES "${_NLOHMANN_JSON_INCLUDE_DIR}"
    )
  endif()
endif()

message(STATUS "nlohmann/json configured successfully")