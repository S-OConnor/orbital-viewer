# backend/ — standalone-configurable: `cmake -S backend -B build-backend`
# works without the top-level project (docs/PLAN.md §3).
if(NOT DEFINED PROJECT_NAME)
  cmake_minimum_required(VERSION 3.20)
  project(olv_backend_standalone LANGUAGES CXX)
  include("${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake")
endif()

# open-dis-cpp: DIS Entity State PDU decode for the "dis" input mode. The
# open_dis_cpp target definition is shared with the simulator (DIS emission,
# docs/FEATURE_INPUT_SOURCES.md Phase 3), hence the common include.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/open_dis_cpp.cmake")

# Core: everything the UDP thread and serializer need (no Beast).
add_library(olv_core STATIC
  src/logger.cpp
  src/state_store.cpp
  src/json_writer.cpp
  src/input_source.cpp
  src/olv1_input_source.cpp
  src/dis_input_source.cpp)
target_include_directories(olv_core PUBLIC src)
target_link_libraries(olv_core
  PUBLIC olv_proto Boost::headers Threads::Threads
  PRIVATE olv_warnings open_dis_cpp)

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
    tests/test_config.cpp
    tests/test_dis_input_source.cpp)
  # olv_net provides config.cpp for test_config.cpp (transitively pulls olv_core).
  target_link_libraries(olv_backend_tests PRIVATE olv_net olv_core olv_test_support olv_warnings)
  add_test(NAME backend_unit COMMAND olv_backend_tests)
endif()
