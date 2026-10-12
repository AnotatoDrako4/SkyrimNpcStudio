add_library(sns_creation STATIC
  src/forge_project.cpp)
add_library(sns::creation ALIAS sns_creation)
target_include_directories(sns_creation PUBLIC include)
target_link_libraries(sns_creation
  PUBLIC sns::projects sns::core nlohmann_json::nlohmann_json
  PRIVATE sns::compile_options)
