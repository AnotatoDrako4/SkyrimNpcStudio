add_library(sns_ipc STATIC
  src/framing.cpp
  src/json_rpc_client.cpp
  src/named_pipe_transport.cpp
  src/worker_process.cpp)
add_library(sns::ipc ALIAS sns_ipc)
target_include_directories(sns_ipc PUBLIC include)
target_link_libraries(sns_ipc
  PUBLIC sns::core nlohmann_json::nlohmann_json
  PRIVATE sns::compile_options)
