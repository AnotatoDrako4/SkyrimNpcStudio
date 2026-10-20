add_library(sns_mo2 STATIC
  src/ini.cpp
  src/instance.cpp
  src/game.cpp
  src/profile.cpp
  src/plugin_header.cpp
  src/vfs.cpp
  src/load_order.cpp)
add_library(sns::mo2 ALIAS sns_mo2)
target_include_directories(sns_mo2 PUBLIC include)
target_link_libraries(sns_mo2
  PUBLIC sns::core
  PRIVATE sns::compile_options version shell32 ole32 advapi32)
