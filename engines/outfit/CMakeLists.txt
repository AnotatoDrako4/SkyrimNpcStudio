add_library(sns_outfit STATIC
  src/outfit_draft.cpp
  src/outfit_presets.cpp
  src/loose_outfit_indexer.cpp)
add_library(sns::outfit ALIAS sns_outfit)
target_include_directories(sns_outfit PUBLIC include)
target_link_libraries(sns_outfit
  PUBLIC sns::projects sns::core sns::mo2 sns::assets nlohmann_json::nlohmann_json
  PRIVATE sns::nif nifly sns::compile_options)
