add_library(sns_appearance STATIC
  src/appearance_draft.cpp
  src/compatibility_guardian.cpp
  src/control_map.cpp
  src/easy_mod.cpp
  src/review_case.cpp
  src/skin_match.cpp
  src/source_matrix.cpp
  src/source_draft.cpp)
add_library(sns::appearance ALIAS sns_appearance)
target_include_directories(sns_appearance PUBLIC include)
target_link_libraries(sns_appearance
  PUBLIC sns::projects sns::outfit sns::face
  PRIVATE sns::compile_options)
