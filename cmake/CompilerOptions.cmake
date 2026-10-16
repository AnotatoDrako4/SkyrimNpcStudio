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

  # Third-party code (Catch2, nifly, nlohmann_json, ...) is compiled with the cache's CMAKE_CXX_FLAGS,
  # not with the options above. A build tree whose cache lost its MSVC defaults (empty
  # CMAKE_CXX_FLAGS / CMAKE_EXE_LINKER_FLAGS_DEBUG, e.g. after a bad -D on the command line) builds
  # that code without /EHsc - exceptions are not unwound, Catch2 SKIP() aborts the test process -
  # and links Debug programs without /DEBUG, so there is no .pdb and crash reports name unrelated
  # symbols. Fail early instead of producing a tree that misbehaves later.
  set(_sns_cache_problems "")
  if(NOT "${CMAKE_CXX_FLAGS}" MATCHES "/EHsc")
    string(APPEND _sns_cache_problems "\n  CMAKE_CXX_FLAGS='${CMAKE_CXX_FLAGS}' lacks /EHsc")
  endif()
  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT "${CMAKE_EXE_LINKER_FLAGS_DEBUG}" MATCHES "/[Dd][Ee][Bb][Uu][Gg]")
    string(APPEND _sns_cache_problems "\n  CMAKE_EXE_LINKER_FLAGS_DEBUG='${CMAKE_EXE_LINKER_FLAGS_DEBUG}' lacks /debug (no .pdb)")
  endif()
  if(_sns_cache_problems)
    message(FATAL_ERROR "This build tree's compiler cache is incomplete:${_sns_cache_problems}\n"
                        "Reconfigure it from scratch: cmake --preset <preset> --fresh")
  endif()

  # Release packages: /Brepro replaces the PE/COFF timestamps with a content hash, so a clean
  # rebuild of the same commit gives the same SkyrimNpcStudio.exe (checked by release.ps1 runs).
  option(SNS_REPRODUCIBLE "Build byte-reproducible binaries (/Brepro)" OFF)
  if(SNS_REPRODUCIBLE)
    add_compile_options(/Brepro)
    add_link_options(/Brepro)
  endif()
else()
  target_compile_options(sns_compile_options INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()
