cmake_minimum_required(VERSION 3.25)

project(SkyrimNpcStudio
  VERSION 0.1.0
  DESCRIPTION "Skyrim NPC Studio Ultimate - NPC appearance and outfit studio for Skyrim SE/AE"
  LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Skyrim NPC Studio targets Windows x64 only (named pipes, MO2, Skyrim SE).")
endif()

# Ninja tracks header dependencies through the /showIncludes prefix of cl.exe. A localized prefix
# (non-ASCII, e.g. Spanish "Nota: inclusión del archivo:") is stored by CMake after converting cl's
# output from the console code page to UTF-8, while Ninja compares raw bytes at build time: the
# two only agree by luck, otherwise header changes stop recompiling their users (stale objects
# with old struct layouts get linked; see docs/CRASH_DIAGNOSTICS.md, test "build:header
# dependencies"). Measure the prefix exactly as the compiler writes it in the console that is
# configuring (ENCODING NONE keeps the raw bytes); builds must then run in a console with the same
# code page, which scripts/dev-env.ps1 guarantees (UTF-8).
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND NOT "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}" MATCHES "^[ -~]*$")
  set(_sns_probe "${CMAKE_BINARY_DIR}/CMakeFiles/sns_showincludes")
  file(WRITE "${_sns_probe}/sns_showincludes_probe.h" "#pragma once\n")
  file(WRITE "${_sns_probe}/probe.cpp" "#include \"sns_showincludes_probe.h\"\n")
  execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c probe.cpp "/Fo${_sns_probe}/probe.obj"
                  WORKING_DIRECTORY "${_sns_probe}" OUTPUT_VARIABLE _sns_out ERROR_QUIET ENCODING NONE)
  string(FIND "${_sns_out}" "sns_showincludes_probe.h" _sns_pos)
  if(_sns_pos GREATER 0)
    string(SUBSTRING "${_sns_out}" 0 ${_sns_pos} _sns_head)
    string(REGEX REPLACE "[A-Za-z]:[^:]*$" "" _sns_head "${_sns_head}")   # drop the include path
    string(REGEX MATCH "[^\r\n]*$" _sns_prefix "${_sns_head}")             # the line holding the prefix
    string(STRIP "${_sns_prefix}" _sns_prefix)
    if(_sns_prefix)
      set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_sns_prefix}")   # the one the Ninja generator reads
      set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${_sns_prefix}")
      set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${_sns_prefix}")
      message(STATUS "Localized MSVC: /showIncludes prefix measured in this console (${_sns_prefix}); "
                     "build from a console with the same code page (scripts/dev-env.ps1).")
    endif()
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(SNS_BUILD_APP "Build the Qt Quick desktop application (requires Qt 6.11)" ON)
option(SNS_BUILD_WORKER "Publish the .NET Bethesda worker with the dotnet CLI" ON)
option(SNS_BUILD_TESTS "Build unit and integration tests" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(CompilerOptions)
include(Dependencies)

add_subdirectory(engines)
add_subdirectory(tools/sns-probe)

if(SNS_BUILD_WORKER)
  include(Worker)
endif()

if(SNS_BUILD_APP)
  add_subdirectory(apps/desktop)
endif()

if(SNS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
