# tooling.cmake — format / format-check / lint convenience targets (no-ops
# with a notice when tools are absent; brew's clang-format is keg-only, hence
# the extra HINTS path). Included from the top-level CMakeLists.txt.

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
