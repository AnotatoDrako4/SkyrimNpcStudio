add_executable(sns-probe main.cpp)
target_link_libraries(sns-probe PRIVATE sns::assets sns::textures sns::face sns::core nifly sns::compile_options)
