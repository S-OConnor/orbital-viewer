# tools/simulator/ — standalone-configurable: `cmake -S tools/simulator -B build-sim`
# works without the top-level project (docs/PLAN.md §3). In a full build the
# top-level CMakeLists.txt provides the toolchain settings, shared INTERFACE
# targets, and dependencies below.
if(NOT DEFINED PROJECT_NAME)
  cmake_minimum_required(VERSION 3.20)
  project(olv_sim_standalone LANGUAGES CXX)

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
  if(DEFINED ENV{HOMEBREW_PREFIX} AND EXISTS "$ENV{HOMEBREW_PREFIX}")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOMEBREW_PREFIX}")
  elseif(EXISTS "/home/linuxbrew/.linuxbrew")
    list(APPEND CMAKE_PREFIX_PATH "/home/linuxbrew/.linuxbrew")
  endif()

  get_filename_component(_olv_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  add_library(olv_proto INTERFACE)
  target_include_directories(olv_proto INTERFACE "${_olv_repo_root}/include")
  add_library(olv_test_support INTERFACE)
  target_include_directories(olv_test_support INTERFACE "${_olv_repo_root}/test/support")
  add_library(olv_warnings INTERFACE)
  target_compile_options(olv_warnings INTERFACE -Wall -Wextra -Wpedantic)
  if(OLV_WERROR)
    target_compile_options(olv_warnings INTERFACE -Werror)
  endif()

  find_package(Threads REQUIRED)
  find_package(Boost 1.74 REQUIRED CONFIG)
  # open-dis-cpp: DIS Entity State PDU encode for `--protocol dis`
  # (docs/features/FEATURE_INPUT_SOURCES.md Phase 3).
  find_package(OpenDIS 1.2 REQUIRED CONFIG)
endif()

# CSV parsing, frame building, and synthetic generation are pure (no Boost);
# only main.cpp touches the network. OpenDIS::OpenDIS6 is PUBLIC so the DIS-builder
# tests can decode the emitted PDUs with the same library.
add_library(olv_sim_lib STATIC
  src/csv_reader.cpp
  src/frame_builder.cpp
  src/generator.cpp
  src/dis_builder.cpp
  src/sim_config.cpp)
target_include_directories(olv_sim_lib PUBLIC src)
target_link_libraries(olv_sim_lib PUBLIC olv_proto OpenDIS::OpenDIS6 PRIVATE olv_warnings)

add_executable(olv_sim src/main.cpp)
target_link_libraries(olv_sim PRIVATE olv_sim_lib Boost::headers Threads::Threads olv_warnings)
# `cmake --install <build> --component simulator` stages the runtime image's
# contents (scripts/build.sh -> build/dist/simulator,
# containers/Dockerfile.simulator); the example CSVs let --csv replay work
# without a bind mount.
install(TARGETS olv_sim RUNTIME DESTINATION bin COMPONENT simulator)
install(DIRECTORY data/ DESTINATION share/olv/data COMPONENT simulator)

if(OLV_BUILD_TESTS)
  add_executable(olv_sim_tests
    tests/test_csv_reader.cpp
    tests/test_frame_builder.cpp
    tests/test_dis_builder.cpp
    tests/test_sim_config.cpp)
  target_link_libraries(olv_sim_tests PRIVATE olv_sim_lib olv_test_support olv_warnings)
  add_test(NAME sim_unit COMMAND olv_sim_tests)
endif()
