# common.cmake — shared toolchain setup for the top-level build (backend) and
# for standalone configuration of tools/simulator/ (see docs/PLAN.md §3,
# "Independent buildability").

if(DEFINED OLV_COMMON_INCLUDED)
  return()
endif()
set(OLV_COMMON_INCLUDED TRUE)

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

find_package(Threads REQUIRED)
# CONFIG mode: FindBoost was removed in CMake 4; BoostConfig.cmake ships with
# Boost >= 1.70 on all supported platforms. Header-only usage (Asio/Beast).
find_package(Boost 1.74 REQUIRED CONFIG)

get_filename_component(OLV_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Public headers (include/olv/, incl. the wire protocol), shared everywhere.
if(NOT TARGET olv_proto)
  add_library(olv_proto INTERFACE)
  target_include_directories(olv_proto INTERFACE "${OLV_REPO_ROOT}/include")
endif()

# Minimal in-repo test framework (test/support/olv_test.hpp).
if(NOT TARGET olv_test_support)
  add_library(olv_test_support INTERFACE)
  target_include_directories(olv_test_support INTERFACE "${OLV_REPO_ROOT}/test/support")
endif()

# Common warning set.
if(NOT TARGET olv_warnings)
  add_library(olv_warnings INTERFACE)
  target_compile_options(olv_warnings INTERFACE -Wall -Wextra -Wpedantic)
  if(OLV_WERROR)
    target_compile_options(olv_warnings INTERFACE -Werror)
  endif()
endif()
