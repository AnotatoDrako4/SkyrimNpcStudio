# Shared compile settings for every first-party C++ target.
add_library(sns_compile_options INTERFACE)
add_library(sns::compile_options ALIAS sns_compile_options)

target_compile_definitions(sns_compile_options INTERFACE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00)

if(MSVC)
  target_compile_options(sns_compile_options INTERFACE
    /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /diagnostics:column)
else()
  target_compile_options(sns_compile_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()
