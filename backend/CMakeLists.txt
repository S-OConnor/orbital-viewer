# backend/ — standalone-configurable: `cmake -S backend -B build-backend`
# works without the top-level project (docs/PLAN.md §3).
if(NOT DEFINED PROJECT_NAME)
  cmake_minimum_required(VERSION 3.20)
  project(olv_backend_standalone LANGUAGES CXX)
  include("${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake")
endif()

# Core: everything the UDP thread and serializer need (no Beast).
add_library(olv_core STATIC
  src/logger.cpp
  src/state_store.cpp
  src/json_writer.cpp
  src/udp_receiver.cpp)
target_include_directories(olv_core PUBLIC src)
target_link_libraries(olv_core
  PUBLIC olv_proto Boost::headers Threads::Threads
  PRIVATE olv_warnings)

# Net: WebSocket server (Beast) + CLI config.
add_library(olv_net STATIC
  src/ws_server.cpp
  src/config.cpp)
target_link_libraries(olv_net PUBLIC olv_core PRIVATE olv_warnings)

add_executable(olv_backend src/main.cpp)
target_link_libraries(olv_backend PRIVATE olv_net olv_warnings)

# WebSocket probe client used by scripts/integration_test.sh.
add_executable(olv_ws_probe tools/ws_probe.cpp)
target_link_libraries(olv_ws_probe PRIVATE Boost::headers Threads::Threads olv_warnings)

if(OLV_BUILD_TESTS)
  add_executable(olv_backend_tests
    tests/test_protocol.cpp
    tests/test_validation.cpp
    tests/test_state_store.cpp
    tests/test_json_writer.cpp
    tests/test_toml.cpp
    tests/test_config.cpp)
  # olv_net provides config.cpp for test_config.cpp (transitively pulls olv_core).
  target_link_libraries(olv_backend_tests PRIVATE olv_net olv_core olv_test_support olv_warnings)
  add_test(NAME backend_unit COMMAND olv_backend_tests)
endif()
