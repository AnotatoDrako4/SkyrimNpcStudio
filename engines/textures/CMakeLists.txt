add_library(sns_textures STATIC src/dds.cpp)
add_library(sns::textures ALIAS sns_textures)
target_include_directories(sns_textures PUBLIC include)
target_link_libraries(sns_textures PUBLIC sns::core PRIVATE DirectXTex sns::compile_options)
