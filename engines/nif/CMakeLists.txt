add_library(sns_nif STATIC src/mesh.cpp)
add_library(sns::nif ALIAS sns_nif)
target_include_directories(sns_nif PUBLIC include)
# nifly is private: no nifly type appears in the public header.
target_link_libraries(sns_nif PUBLIC sns::core PRIVATE sns::compile_options nifly)
