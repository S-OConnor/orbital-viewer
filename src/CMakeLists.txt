# src/ — backend (olv_backend). Public headers live in include/olv/ (exposed
# through olv_proto, see the top-level CMakeLists.txt). open-dis-cpp provides DIS Entity
# State PDU decode for the "dis" input mode.

# Core: everything the UDP thread and serializer need (no Beast).
add_library(olv_core STATIC
  logger.cpp
  state_store.cpp
  json_writer.cpp
  input_source.cpp
  olv1_input_source.cpp
  dis_input_source.cpp
  olv2_input_source.cpp)
target_link_libraries(olv_core
  PUBLIC olv_proto Boost::headers Threads::Threads
  PRIVATE olv_warnings OpenDIS::OpenDIS6)

# Net: WebSocket server (Beast) + CLI config.
add_library(olv_net STATIC
  ws_server.cpp
  config.cpp)
target_link_libraries(olv_net PUBLIC olv_core PRIVATE olv_warnings)

add_executable(olv_backend main.cpp)
target_link_libraries(olv_backend PRIVATE olv_net olv_warnings)
# Keep the binary at build/olv_backend (docs, scripts).
set_target_properties(olv_backend PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
# `cmake --install <build> --component backend` stages the runtime image's
# contents (scripts/build.sh -> build/dist/backend, containers/Dockerfile.backend).
install(TARGETS olv_backend RUNTIME DESTINATION bin COMPONENT backend)
