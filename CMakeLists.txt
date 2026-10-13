cmake_minimum_required(VERSION 3.25)

project(SkyrimNpcStudio
  VERSION 0.1.0
  DESCRIPTION "Skyrim NPC Studio Ultimate - NPC appearance and outfit studio for Skyrim SE/AE"
  LANGUAGES C CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Skyrim NPC Studio targets Windows x64 only (named pipes, MO2, Skyrim SE).")
endif()

# Ninja tracks header dependencies through the /showIncludes prefix of cl.exe. A localized prefix
# (non-ASCII) only matches when the build runs in a UTF-8 console; otherwise header changes stop
# recompiling their users (see docs/CRASH_DIAGNOSTICS.md and tests "build:header dependencies").
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND NOT "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}" MATCHES "^[ -~]*$")
  message(STATUS "Localized MSVC (/showIncludes prefix '${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}'): build from a UTF-8 "
                 "console (scripts/dev-env.ps1 sets it) or header dependencies are not recorded.")
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
