# tools/ — developer/test-support executables.

# WebSocket probe client used by scripts/integration_test.sh.
add_executable(olv_ws_probe ws_probe.cpp)
target_link_libraries(olv_ws_probe PRIVATE Boost::headers Threads::Threads olv_warnings)
set_target_properties(olv_ws_probe PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

# Simulator (olv_sim) — also configurable on its own (`cmake -S tools/simulator`).
add_subdirectory(simulator)
