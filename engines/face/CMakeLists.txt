add_library(sns_face STATIC
  src/face_morphs.cpp
  src/head_parts.cpp
  src/tri_file.cpp
  src/face_recipe.cpp
  src/facegen.cpp
  src/face_tint.cpp)
add_library(sns::face ALIAS sns_face)
target_include_directories(sns_face PUBLIC include)
# nifly stays private: no nifly type appears in the public headers.
target_link_libraries(sns_face
  PUBLIC sns::core sns::textures nlohmann_json::nlohmann_json
  PRIVATE sns::compile_options nifly)
