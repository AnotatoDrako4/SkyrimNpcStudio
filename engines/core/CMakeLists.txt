add_library(sns_core STATIC
  src/text.cpp
  src/file_io.cpp)
add_library(sns::core ALIAS sns_core)
target_include_directories(sns_core PUBLIC include)
target_link_libraries(sns_core PRIVATE sns::compile_options)
