# src/ — backend (olv_backend). Public headers live in include/olv/ (exposed
# through olv_proto, see cmake/common.cmake). open-dis-cpp provides DIS Entity
# State PDU decode for the "dis" input mode.

# Core: everything the UDP thread and serializer need (no Beast).
add_library(olv_core STATIC
  logger.cpp
  state_store.cpp
  json_writer.cpp
  input_source.cpp
  olv1_input_source.cpp
  dis_input_source.cpp)
target_link_libraries(olv_core
  PUBLIC olv_proto Boost::headers Threads::Threads
  PRIVATE olv_warnings open_dis_cpp)

# Net: WebSocket server (Beast) + CLI config.
add_library(olv_net STATIC
  ws_server.cpp
  config.cpp)
target_link_libraries(olv_net PUBLIC olv_core PRIVATE olv_warnings)

add_executable(olv_backend main.cpp)
target_link_libraries(olv_backend PRIVATE olv_net olv_warnings)
# Keep the binary at build/olv_backend (docs, scripts, Containerfile.cpp).
set_target_properties(olv_backend PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
