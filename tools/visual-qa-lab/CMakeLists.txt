cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SkyrimNpcStudioVisualQaLab VERSION 1.0.0 LANGUAGES C CXX)
  get_filename_component(SNS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

  if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND NOT "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}" MATCHES "^[ -~]*$")
    set(_sns_probe "${CMAKE_BINARY_DIR}/CMakeFiles/sns_showincludes")
    file(WRITE "${_sns_probe}/sns_showincludes_probe.h" "#pragma once\n")
    file(WRITE "${_sns_probe}/probe.cpp" "#include \"sns_showincludes_probe.h\"\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c probe.cpp "/Fo${_sns_probe}/probe.obj"
                    WORKING_DIRECTORY "${_sns_probe}" OUTPUT_VARIABLE _sns_out ERROR_QUIET ENCODING NONE)
    string(FIND "${_sns_out}" "sns_showincludes_probe.h" _sns_pos)
    if(_sns_pos GREATER 0)
      string(SUBSTRING "${_sns_out}" 0 ${_sns_pos} _sns_head)
      string(REGEX REPLACE "[A-Za-z]:[^:]*$" "" _sns_head "${_sns_head}")
      string(REGEX MATCH "[^\r\n]*$" _sns_prefix "${_sns_head}")
      string(STRIP "${_sns_prefix}" _sns_prefix)
      if(_sns_prefix)
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_sns_prefix}")
        set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${_sns_prefix}")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${_sns_prefix}")
      endif()
    endif()
  endif()

  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

  # Reuse already fetched sources in build/debug/_deps if present to avoid network fetches.
  set(_existing_deps "${SNS_REPO_ROOT}/build/debug/_deps")
  if(EXISTS "${_existing_deps}/nlohmann_json-src")
    set(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON "${_existing_deps}/nlohmann_json-src" CACHE PATH "" FORCE)
  endif()
  if(EXISTS "${_existing_deps}/catch2-src")
    set(FETCHCONTENT_SOURCE_DIR_CATCH2 "${_existing_deps}/catch2-src" CACHE PATH "" FORCE)
  endif()
  if(EXISTS "${_existing_deps}/lz4-src")
    set(FETCHCONTENT_SOURCE_DIR_LZ4 "${_existing_deps}/lz4-src" CACHE PATH "" FORCE)
  endif()
  if(EXISTS "${_existing_deps}/zlib-src")
    set(FETCHCONTENT_SOURCE_DIR_ZLIB "${_existing_deps}/zlib-src" CACHE PATH "" FORCE)
  endif()
  if(EXISTS "${_existing_deps}/nifly-src")
    set(FETCHCONTENT_SOURCE_DIR_NIFLY "${_existing_deps}/nifly-src" CACHE PATH "" FORCE)
  endif()
  if(EXISTS "${_existing_deps}/directxtex-src")
    set(FETCHCONTENT_SOURCE_DIR_DIRECTXTEX "${_existing_deps}/directxtex-src" CACHE PATH "" FORCE)
  endif()

  set(SNS_BUILD_TESTS ON CACHE BOOL "" FORCE)
  list(APPEND CMAKE_MODULE_PATH "${SNS_REPO_ROOT}/cmake")
  include(CompilerOptions)
  include(Dependencies)
  add_subdirectory("${SNS_REPO_ROOT}/engines" "${CMAKE_BINARY_DIR}/engines")
endif()

add_library(sns_visual_qa STATIC
  src/clipping_analyzer.cpp
  src/seam_analyzer.cpp
  src/morph_analyzer.cpp
  src/image_comparator.cpp
  src/scene_inspector.cpp
  src/report_writer.cpp
  src/regression_suite.cpp
)

target_include_directories(sns_visual_qa
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(sns_visual_qa
  PUBLIC
    sns::core
    sns::mo2
    sns::ipc
    sns::assets
    sns::nif
    sns::textures
    sns::face
    sns::body
    sns::rendering
    nlohmann_json::nlohmann_json
  PRIVATE
    sns::compile_options
    windowscodecs
    ole32
    psapi
)

add_executable(sns-visual-qa
  cli/main.cpp
)

target_link_libraries(sns-visual-qa
  PRIVATE
    sns_visual_qa
    sns::compile_options
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_subdirectory("${SNS_REPO_ROOT}/tests/visual-qa" "${CMAKE_BINARY_DIR}/tests/visual-qa")
endif()
