# simulator/ — standalone-configurable: `cmake -S simulator -B build-sim`
# works without the top-level project (docs/PLAN.md §3).
if(NOT DEFINED PROJECT_NAME)
  cmake_minimum_required(VERSION 3.20)
  project(olv_sim_standalone LANGUAGES CXX)
  include("${CMAKE_CURRENT_LIST_DIR}/../cmake/common.cmake")
endif()

# open-dis-cpp: DIS Entity State PDU encode for `--protocol dis`
# (docs/FEATURE_INPUT_SOURCES.md Phase 3); target shared with the backend.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/open_dis_cpp.cmake")

# CSV parsing, frame building, and synthetic generation are pure (no Boost);
# only main.cpp touches the network. open_dis_cpp is PUBLIC so the DIS-builder
# tests can decode the emitted PDUs with the same library.
add_library(olv_sim_lib STATIC
  src/csv_reader.cpp
  src/frame_builder.cpp
  src/generator.cpp
  src/dis_builder.cpp
  src/sim_config.cpp)
target_include_directories(olv_sim_lib PUBLIC src)
target_link_libraries(olv_sim_lib PUBLIC olv_proto open_dis_cpp PRIVATE olv_warnings)

add_executable(olv_sim src/main.cpp)
target_link_libraries(olv_sim PRIVATE olv_sim_lib Boost::headers Threads::Threads olv_warnings)

if(OLV_BUILD_TESTS)
  add_executable(olv_sim_tests
    tests/test_csv_reader.cpp
    tests/test_frame_builder.cpp
    tests/test_dis_builder.cpp
    tests/test_sim_config.cpp)
  target_link_libraries(olv_sim_tests PRIVATE olv_sim_lib olv_test_support olv_warnings)
  add_test(NAME sim_unit COMMAND olv_sim_tests)
endif()
