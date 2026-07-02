cmake_minimum_required(VERSION 3.20)
project(orbital_los_viewer VERSION 0.1.0 LANGUAGES CXX)

include(cmake/common.cmake)

add_subdirectory(backend)
add_subdirectory(simulator)

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
     "${CMAKE_CURRENT_SOURCE_DIR}/backend/src/*.[ch]pp"
     "${CMAKE_CURRENT_SOURCE_DIR}/backend/tools/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/backend/tests/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/backend/include/olv/*.hpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/simulator/src/*.[ch]pp"
     "${CMAKE_CURRENT_SOURCE_DIR}/simulator/tests/*.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/tests/support/*.hpp")

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
            -I "${CMAKE_CURRENT_SOURCE_DIR}/backend/include"
            -I "${CMAKE_CURRENT_SOURCE_DIR}/backend/src"
            -I "${CMAKE_CURRENT_SOURCE_DIR}/simulator/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/backend/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/simulator/src"
    COMMENT "cppcheck"
    VERBATIM)
else()
  add_custom_target(lint COMMAND ${CMAKE_COMMAND} -E echo "cppcheck not found")
endif()
