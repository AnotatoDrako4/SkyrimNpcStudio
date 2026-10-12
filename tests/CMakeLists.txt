include(Catch)

# ---- Unit tests: engines, no external processes, synthetic data only ---------------------------
add_executable(sns_engine_tests
  unit/engines/test_text.cpp
  unit/engines/test_framing.cpp
  unit/engines/test_json_rpc_client.cpp
  unit/engines/test_ini.cpp
  unit/engines/test_instance_profile.cpp
  unit/engines/test_plugin_header.cpp
  unit/engines/test_vfs_load_order.cpp
  unit/engines/test_projects_outfit.cpp
  unit/engines/test_appearance_draft.cpp
  unit/engines/test_presets.cpp
  unit/engines/test_face_engine.cpp)
target_sources(sns_engine_tests PRIVATE unit/engines/test_assets_rendering.cpp)
target_include_directories(sns_engine_tests PRIVATE support)
target_link_libraries(sns_engine_tests PRIVATE
  sns::core sns::ipc sns::mo2 sns::projects sns::outfit sns::appearance sns::presets sns::face sns::rendering nifly lz4 sns::compile_options Catch2::Catch2WithMain)
catch_discover_tests(sns_engine_tests TEST_PREFIX "unit:" DISCOVERY_MODE PRE_TEST)

# ---- Integration tests: real worker process over a real named pipe -----------------------------
if(SNS_BUILD_WORKER)
  set(SNS_FIXTURE_GEN_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fixturegen)
  set(SNS_FIXTURE_GEN_EXE ${SNS_FIXTURE_GEN_DIR}/SkyrimNpcStudio.FixtureGen.exe)
  file(GLOB_RECURSE SNS_FIXTURE_SOURCES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/tools/FixtureGen/*.cs ${PROJECT_SOURCE_DIR}/tests/fixtures/*.cs)
  add_custom_command(
    OUTPUT ${SNS_FIXTURE_GEN_EXE}
    COMMAND ${SNS_DOTNET} publish ${PROJECT_SOURCE_DIR}/tools/FixtureGen/FixtureGen.csproj
            --configuration Debug --output ${SNS_FIXTURE_GEN_DIR} --nologo -v quiet
    DEPENDS ${SNS_FIXTURE_SOURCES} ${PROJECT_SOURCE_DIR}/tools/FixtureGen/FixtureGen.csproj
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Publishing synthetic fixture generator"
    VERBATIM)
  add_custom_target(fixture_gen ALL DEPENDS ${SNS_FIXTURE_GEN_EXE})

  add_executable(sns_integration_tests
    integration/test_worker_roundtrip.cpp
    integration/test_local_mo2.cpp)
  target_include_directories(sns_integration_tests PRIVATE support)
  target_compile_definitions(sns_integration_tests PRIVATE
    SNS_WORKER_EXE="${SNS_WORKER_EXE}"
    SNS_FIXTURE_GEN_EXE="${SNS_FIXTURE_GEN_EXE}")
  target_link_libraries(sns_integration_tests PRIVATE
    sns::core sns::ipc sns::mo2 sns::rendering sns::compile_options Catch2::Catch2WithMain)
  add_dependencies(sns_integration_tests bethesda_worker fixture_gen)
  catch_discover_tests(sns_integration_tests TEST_PREFIX "integration:" DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 600)
endif()
