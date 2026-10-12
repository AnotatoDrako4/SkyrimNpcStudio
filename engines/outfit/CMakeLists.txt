add_library(sns_outfit STATIC
  src/outfit_draft.cpp)
add_library(sns::outfit ALIAS sns_outfit)
target_include_directories(sns_outfit PUBLIC include)
target_link_libraries(sns_outfit
  PUBLIC sns::projects
  PRIVATE sns::compile_options)
