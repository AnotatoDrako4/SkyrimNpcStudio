add_library(sns_presets STATIC
  src/preset_model.cpp
  src/jslot_parser.cpp
  src/export_head_match.cpp)
add_library(sns::presets ALIAS sns_presets)
target_include_directories(sns_presets PUBLIC include)
target_link_libraries(sns_presets
  PUBLIC sns::core sns::face sns::appearance nlohmann_json::nlohmann_json
  PRIVATE sns::compile_options)
