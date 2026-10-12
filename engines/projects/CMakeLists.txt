add_library(sns_projects STATIC
  src/command_stack.cpp
  src/project.cpp)
add_library(sns::projects ALIAS sns_projects)
target_include_directories(sns_projects PUBLIC include)
target_link_libraries(sns_projects
  PUBLIC sns::core nlohmann_json::nlohmann_json
  PRIVATE sns::compile_options)
