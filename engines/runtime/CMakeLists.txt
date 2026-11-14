add_library(sns_runtime STATIC
  src/runtime_report.cpp
  src/log_reader.cpp
  src/correlator.cpp)
add_library(sns::runtime ALIAS sns_runtime)
target_include_directories(sns_runtime PUBLIC include)
target_link_libraries(sns_runtime PUBLIC sns::core nlohmann_json::nlohmann_json PRIVATE sns::compile_options)
