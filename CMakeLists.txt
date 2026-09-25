cmake_minimum_required(VERSION 3.20)
project(orbital_los_viewer VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Project-wide setup: C++ standard, build type, options, shared INTERFACE
# targets (olv_proto, olv_warnings, olv_test_support).
# ---------------------------------------------------------------------------
include(cmake/common.cmake)

# ---------------------------------------------------------------------------
# Dependencies.
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
# CONFIG mode: FindBoost was removed in CMake 4; BoostConfig.cmake ships with
# Boost >= 1.70 on all supported platforms. Header-only usage (Asio/Beast).
find_package(Boost 1.74 REQUIRED CONFIG)
# open-dis-cpp (imported `open_dis_cpp` target): DIS decode in the backend,
# DIS emission in the simulator.
include(cmake/open_dis_cpp.cmake)

# ---------------------------------------------------------------------------
# Components. Each subdirectory lists its own sources and target options.
# ---------------------------------------------------------------------------
add_subdirectory(src)     # olv_core, olv_net, olv_backend
add_subdirectory(tools)   # olv_ws_probe, tools/simulator (olv_sim)
if(OLV_BUILD_TESTS)
  add_subdirectory(test)  # olv_backend_tests, integration test
endif()

# format / format-check / lint convenience targets.
include(cmake/tooling.cmake)
