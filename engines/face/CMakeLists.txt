add_library(sns_face STATIC
  src/face_morphs.cpp
  src/head_parts.cpp)
add_library(sns::face ALIAS sns_face)
target_include_directories(sns_face PUBLIC include)
target_link_libraries(sns_face
  PUBLIC sns::core
  PRIVATE sns::compile_options)
