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
  unit/engines/test_character_forge.cpp
  unit/engines/test_appearance_draft.cpp
  unit/engines/test_presets.cpp
  unit/engines/test_face_engine.cpp
  unit/engines/test_facegen.cpp
  unit/engines/test_outfit_presets.cpp
  unit/engines/test_compatibility_guardian.cpp
  unit/engines/test_control_map.cpp
  unit/engines/test_easy_mod.cpp)
target_sources(sns_engine_tests PRIVATE unit/engines/test_assets_rendering.cpp unit/engines/test_gltf_inspect.cpp)
target_compile_definitions(sns_engine_tests PRIVATE SNS_TEST_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/models")
target_include_directories(sns_engine_tests PRIVATE support)
target_link_libraries(sns_engine_tests PRIVATE
  sns::core sns::ipc sns::mo2 sns::projects sns::creation sns::outfit sns::appearance sns::presets sns::face sns::rendering
  sns::gltf nlohmann_json::nlohmann_json nifly lz4 sns::compile_options Catch2::Catch2WithMain)
catch_discover_tests(sns_engine_tests TEST_PREFIX "unit:" DISCOVERY_MODE PRE_TEST)

# ---- Build tree: every first-party object has its header dependencies recorded ---------------
if(CMAKE_GENERATOR MATCHES "Ninja")
  add_test(NAME "build:header dependencies recorded"
    COMMAND ${CMAKE_COMMAND} -DNINJA=${CMAKE_MAKE_PROGRAM} -DBUILD_DIR=${CMAKE_BINARY_DIR}
            -P ${PROJECT_SOURCE_DIR}/cmake/CheckHeaderDeps.cmake)
endif()

# ---- Desktop app: the crash handler writes a report and a minidump ----------------------------
if(SNS_BUILD_APP AND WIN32)
  find_package(Qt6 6.11 REQUIRED COMPONENTS Core)
  add_executable(sns_forge_asset_tests
    app/test_forge_asset_library.cpp
    app/test_forge_blender_bridge.cpp
    ${PROJECT_SOURCE_DIR}/apps/desktop/viewmodels/forge_blender_bridge.cpp
    ${PROJECT_SOURCE_DIR}/apps/desktop/viewmodels/forge_asset_library.cpp)
  set_target_properties(sns_forge_asset_tests PROPERTIES AUTOMOC ON)
  set_source_files_properties(${PROJECT_SOURCE_DIR}/tools/blender/forge_bridge.py PROPERTIES QT_RESOURCE_ALIAS forge_bridge.py)
  qt_add_resources(sns_forge_asset_tests forge_bridge_test_script PREFIX "/forge"
    FILES ${PROJECT_SOURCE_DIR}/tools/blender/forge_bridge.py)
  target_include_directories(sns_forge_asset_tests PRIVATE support ${PROJECT_SOURCE_DIR}/apps/desktop/viewmodels)
  target_compile_definitions(sns_forge_asset_tests PRIVATE SNS_TEST_MODELS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/models")
  target_link_libraries(sns_forge_asset_tests PRIVATE
    Qt6::Core sns::creation sns::gltf sns::compile_options Catch2::Catch2WithMain)
  catch_discover_tests(sns_forge_asset_tests TEST_PREFIX "app:" DISCOVERY_MODE PRE_TEST)

  # Workbench preferences (interface scale, fonts, panel layouts): pure Qt Core, no window needed.
  find_package(Qt6 6.11 REQUIRED COMPONENTS Qml)
  add_executable(sns_workbench_tests
    app/test_workbench_state.cpp
    ${PROJECT_SOURCE_DIR}/apps/desktop/viewmodels/workbench_state.cpp)
  set_target_properties(sns_workbench_tests PROPERTIES AUTOMOC ON)
  target_include_directories(sns_workbench_tests PRIVATE support ${PROJECT_SOURCE_DIR}/apps/desktop/viewmodels)
  target_link_libraries(sns_workbench_tests PRIVATE Qt6::Core Qt6::Qml sns::compile_options Catch2::Catch2WithMain)
  catch_discover_tests(sns_workbench_tests TEST_PREFIX "app:" DISCOVERY_MODE PRE_TEST)

  foreach(kind access-violation terminate abort)
    add_test(NAME "app:crash report and minidump (${kind})"
      COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:SkyrimNpcStudio> -DKIND=${kind}
              -DWORK=${CMAKE_CURRENT_BINARY_DIR}/crash-selftest-${kind}
              -P ${CMAKE_CURRENT_SOURCE_DIR}/app/crash_selftest.cmake)
    set_tests_properties("app:crash report and minidump (${kind})" PROPERTIES TIMEOUT 180)
  endforeach()
endif()

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
    integration/test_local_mo2.cpp
    integration/test_local_facegen.cpp)
  target_include_directories(sns_integration_tests PRIVATE support)
  target_compile_definitions(sns_integration_tests PRIVATE
    SNS_WORKER_EXE="${SNS_WORKER_EXE}"
    SNS_FIXTURE_GEN_EXE="${SNS_FIXTURE_GEN_EXE}")
  target_link_libraries(sns_integration_tests PRIVATE
    sns::core sns::ipc sns::mo2 sns::rendering sns::face sns::compile_options Catch2::Catch2WithMain)
  add_dependencies(sns_integration_tests bethesda_worker fixture_gen)
  catch_discover_tests(sns_integration_tests TEST_PREFIX "integration:" DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 600)
endif()
