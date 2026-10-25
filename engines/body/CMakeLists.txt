add_library(sns_body STATIC
  src/xml_lite.cpp
  src/bodyslide_presets.cpp
  src/bcng_rules.cpp
  src/body_analysis.cpp)
add_library(sns::body ALIAS sns_body)
target_include_directories(sns_body PUBLIC include)
target_link_libraries(sns_body
  PUBLIC sns::core sns::mo2 nlohmann_json::nlohmann_json
  PRIVATE sns::nif sns::compile_options)
