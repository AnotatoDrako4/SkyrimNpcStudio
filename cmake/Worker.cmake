# Publishes the .NET Bethesda worker next to the studio executable (bin/worker/).
find_program(SNS_DOTNET dotnet HINTS "$ENV{DOTNET_ROOT}" REQUIRED)

set(SNS_WORKER_PROJECT ${PROJECT_SOURCE_DIR}/services/BethesdaWorker/BethesdaWorker.csproj)
set(SNS_WORKER_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/worker)
set(SNS_WORKER_EXE ${SNS_WORKER_DIR}/SkyrimNpcStudio.BethesdaWorker.exe)
file(GLOB_RECURSE SNS_WORKER_SOURCES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/services/BethesdaWorker/*.cs
  ${PROJECT_SOURCE_DIR}/services/BethesdaWorker/*.csproj)

add_custom_command(
  OUTPUT ${SNS_WORKER_EXE}
  COMMAND ${SNS_DOTNET} publish ${SNS_WORKER_PROJECT}
          --configuration $<IF:$<CONFIG:Debug>,Debug,Release>
          --output ${SNS_WORKER_DIR} --nologo -v quiet
  DEPENDS ${SNS_WORKER_SOURCES}
          ${PROJECT_SOURCE_DIR}/Directory.Packages.props
          ${PROJECT_SOURCE_DIR}/Directory.Build.props
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  COMMENT "Publishing BethesdaWorker (.NET 10)"
  VERBATIM)

add_custom_target(bethesda_worker ALL DEPENDS ${SNS_WORKER_EXE})
