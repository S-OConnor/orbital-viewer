# test/ — backend unit tests and the end-to-end integration test. Only added
# when OLV_BUILD_TESTS is ON (see top-level CMakeLists.txt). Simulator unit
# tests live with the simulator (tools/simulator/tests/) so it stays
# standalone-configurable.

add_executable(olv_backend_tests
  test_protocol.cpp
  test_validation.cpp
  test_state_store.cpp
  test_json_writer.cpp
  test_toml.cpp
  test_config.cpp
  test_dis_input_source.cpp)
# olv_net provides config.cpp for test_config.cpp (transitively pulls olv_core).
target_link_libraries(olv_backend_tests PRIVATE olv_net olv_core olv_test_support olv_warnings)
set_target_properties(olv_backend_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
add_test(NAME backend_unit COMMAND olv_backend_tests)

# Integration test: simulator -> backend -> ws_probe -> frontend JSON check.
if(UNIX)
  add_test(NAME integration
           COMMAND bash "${PROJECT_SOURCE_DIR}/scripts/integration_test.sh"
                   "$<TARGET_FILE:olv_backend>" "$<TARGET_FILE:olv_sim>"
                   "$<TARGET_FILE:olv_ws_probe>" "${PROJECT_SOURCE_DIR}")
  set_tests_properties(integration PROPERTIES TIMEOUT 120 LABELS integration)
endif()
