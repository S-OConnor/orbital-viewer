cmake_minimum_required(VERSION 3.20)
project(orbital_los_viewer VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Toolchain and options.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(OLV_BUILD_TESTS "Build unit tests" ON)
option(OLV_WERROR "Treat warnings as errors" OFF)
if(OLV_BUILD_TESTS)
  enable_testing()
endif()

# Convenience: pick up Homebrew/Linuxbrew installs (e.g. Boost on immutable
# distros) without requiring -DCMAKE_PREFIX_PATH on every configure.
if(DEFINED ENV{HOMEBREW_PREFIX} AND EXISTS "$ENV{HOMEBREW_PREFIX}")
  list(APPEND CMAKE_PREFIX_PATH "$ENV{HOMEBREW_PREFIX}")
elseif(EXISTS "/home/linuxbrew/.linuxbrew")
  list(APPEND CMAKE_PREFIX_PATH "/home/linuxbrew/.linuxbrew")
endif()

# ---------------------------------------------------------------------------
# Shared INTERFACE targets.
# ---------------------------------------------------------------------------
# Public headers (include/olv/, incl. the wire protocol), shared everywhere.
add_library(olv_proto INTERFACE)
target_include_directories(olv_proto INTERFACE "${PROJECT_SOURCE_DIR}/include")

# Minimal in-repo test framework (test/support/olv_test.hpp), used by the
# simulator unit tests. The backend unit tests use GoogleTest (below).
add_library(olv_test_support INTERFACE)
target_include_directories(olv_test_support INTERFACE "${PROJECT_SOURCE_DIR}/test/support")

# Common warning set.
add_library(olv_warnings INTERFACE)
target_compile_options(olv_warnings INTERFACE -Wall -Wextra -Wpedantic)
if(OLV_WERROR)
  target_compile_options(olv_warnings INTERFACE -Werror)
endif()

# ---------------------------------------------------------------------------
# Dependencies.
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
# CONFIG mode: FindBoost was removed in CMake 4; BoostConfig.cmake ships with
# Boost >= 1.70 on all supported platforms. Header-only usage (Asio/Beast).
find_package(Boost 1.74 REQUIRED CONFIG)
# open-dis-cpp (OpenDIS::OpenDIS6): DIS decode in the backend, DIS emission in
# the simulator. Upstream's own package config, installed by
# containers/Dockerfile.builder (built from the pinned release into /usr/local).
find_package(OpenDIS 1.2 REQUIRED CONFIG)
# GoogleTest for the backend unit tests (test/). Only needed when tests are
# built: gtest-devel from EPEL in containers/Dockerfile.builder, `brew install
# googletest` on the host.
if(OLV_BUILD_TESTS)
  find_package(GTest 1.14 REQUIRED CONFIG)
  include(GoogleTest)
endif()

# ---------------------------------------------------------------------------
# Components. Each subdirectory lists its own sources and target options.
# ---------------------------------------------------------------------------
add_subdirectory(src)     # olv_core, olv_net, olv_backend
add_subdirectory(tools)   # olv_ws_probe, tools/simulator (olv_sim)
if(OLV_BUILD_TESTS)
  add_subdirectory(test)  # olv_backend_tests, integration test
endif()

# ---------------------------------------------------------------------------
# format / format-check / lint convenience targets (no-ops with a notice when
# the tools are absent; brew's clang-format is keg-only, hence the HINTS).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE OLV_CXX_SOURCES
     "${PROJECT_SOURCE_DIR}/include/olv/*.hpp"
     "${PROJECT_SOURCE_DIR}/src/*.cpp"
     "${PROJECT_SOURCE_DIR}/test/*.cpp"
     "${PROJECT_SOURCE_DIR}/test/support/*.hpp"
     "${PROJECT_SOURCE_DIR}/tools/*.cpp"
     "${PROJECT_SOURCE_DIR}/tools/simulator/src/*.[ch]pp"
     "${PROJECT_SOURCE_DIR}/tools/simulator/tests/*.cpp")

find_program(OLV_CLANG_FORMAT clang-format
             HINTS /home/linuxbrew/.linuxbrew/opt/clang-format/bin)
if(OLV_CLANG_FORMAT)
  add_custom_target(format
    COMMAND ${OLV_CLANG_FORMAT} -i ${OLV_CXX_SOURCES}
    COMMENT "clang-format (in place)"
    VERBATIM)
  add_custom_target(format-check
    COMMAND ${OLV_CLANG_FORMAT} --dry-run --Werror ${OLV_CXX_SOURCES}
    COMMENT "clang-format (check only)"
    VERBATIM)
else()
  add_custom_target(format COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found")
endif()

find_program(OLV_CPPCHECK cppcheck)
if(OLV_CPPCHECK)
  add_custom_target(lint
    COMMAND ${OLV_CPPCHECK} --enable=warning,performance,portability
            --std=c++20 --inline-suppr --error-exitcode=1 --quiet
            --suppress=missingIncludeSystem
            -I "${PROJECT_SOURCE_DIR}/include"
            -I "${PROJECT_SOURCE_DIR}/tools/simulator/src"
            "${PROJECT_SOURCE_DIR}/src"
            "${PROJECT_SOURCE_DIR}/tools/simulator/src"
    COMMENT "cppcheck"
    VERBATIM)
else()
  add_custom_target(lint COMMAND ${CMAKE_COMMAND} -E echo "cppcheck not found")
endif()
