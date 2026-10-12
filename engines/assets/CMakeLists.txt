add_library(sns_assets STATIC
  src/bsa_archive.cpp
  src/asset_resolver.cpp)
add_library(sns::assets ALIAS sns_assets)
target_include_directories(sns_assets PUBLIC include)
target_link_libraries(sns_assets
  PUBLIC sns::core sns::mo2
  PRIVATE sns::compile_options lz4)
