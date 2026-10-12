add_library(sns_rendering src/scene.cpp)
add_library(sns::rendering ALIAS sns_rendering)
target_include_directories(sns_rendering PUBLIC include)
target_link_libraries(sns_rendering PUBLIC sns::nif sns::textures sns::assets nlohmann_json::nlohmann_json PRIVATE sns::compile_options)
