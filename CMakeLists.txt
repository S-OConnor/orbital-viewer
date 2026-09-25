cmake_minimum_required(VERSION 3.20)
project(orbital_los_viewer VERSION 0.1.0 LANGUAGES CXX)

include(cmake/common.cmake)

include(cmake/open_dis_cpp.cmake)

# ---------------------------------------------------------------------------
# Backend (olv_backend). Public headers live in include/olv/ (exposed through
# olv_proto, see cmake/common.cmake); sources in src/, unit tests in test/.
# open-dis-cpp provides DIS Entity State PDU decode for the "dis" input mode;
# the open_dis_cpp target is shared with the simulator (DIS emission,
# docs/features/FEATURE_INPUT_SOURCES.md Phase 3).
# ---------------------------------------------------------------------------

# Core: everything the UDP thread and serializer need (no Beast).
add_library(olv_core STATIC
  src/logger.cpp
  src/state_store.cpp
  src/json_writer.cpp
  src/input_source.cpp
  src/olv1_input_source.cpp
  src/dis_input_source.cpp)
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
    test/test_protocol.cpp
    test/test_validation.cpp
    test/test_state_store.cpp
    test/test_json_writer.cpp
    test/test_toml.cpp
    test/test_config.cpp
    test/test_dis_input_source.cpp)
  # olv_net provides config.cpp for test_config.cpp (transitively pulls olv_core).
  target_link_libraries(olv_backend_tests PRIVATE olv_net olv_core olv_test_support olv_warnings)
  add_test(NAME backend_unit COMMAND olv_backend_tests)
endif()

# ---------------------------------------------------------------------------
# Simulator (olv_sim) — standalone-configurable dev tool under tools/.
# ---------------------------------------------------------------------------
add_subdirectory(tools/simulator)

# ---------------------------------------------------------------------------
# Integration test: simulator -> backend -> ws_probe -> frontend JSON check.
# ---------------------------------------------------------------------------
if(OLV_BUILD_TESTS AND UNIX)
  add_test(NAME integration
           COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scripts/integration_test.sh"
                   "$<TARGET_FILE:olv_backend>" "$<TARGET_FILE:olv_sim>"
                   "$<TARGET_FILE:olv_ws_probe>" "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(integration PROPERTIES TIMEOUT 120 LABELS integration)
endif()

# ---------------------------------------------------------------------------
# format / lint convenience targets (no-ops with a notice when tools are
# absent; brew's clang-format is keg-only, hence the extra HINTS path).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE OLV_CXX_SOURCES
     "${CMAKE_CURRENT_SOURCE_DIR}/include/olv/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/test/support/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tools/simulator/src/*.[ch]pp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tools/simulator/tests/*.cpp")

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
            -I "${CMAKE_CURRENT_SOURCE_DIR}/include"
            -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/simulator/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/simulator/src"
    COMMENT "cppcheck"
    VERBATIM)
else()
  add_custom_target(lint COMMAND ${CMAKE_COMMAND} -E echo "cppcheck not found")
endif()
