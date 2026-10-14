add_library(sns_gltf STATIC
  src/gltf_inspect.cpp)
add_library(sns::gltf ALIAS sns_gltf)
target_include_directories(sns_gltf PUBLIC include)
target_link_libraries(sns_gltf
  PUBLIC sns::core
  PRIVATE nlohmann_json::nlohmann_json sns::compile_options)
