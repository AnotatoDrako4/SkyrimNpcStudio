# Reference integration (GenerateFollower / NPC Plugin Chooser 2 / Effigy study): tests of the Appearance Source
# Matrix and its non-destructive A/B draft. Synthetic data only, no external process, no Qt, no game files.
# Kept in its own folder (own CMakeLists) so the shared tests/CMakeLists.txt stays untouched.
include(Catch)

add_executable(sns_reference_tests
  test_source_matrix.cpp
  test_source_draft.cpp
  test_confined_file.cpp
  test_export_head_match.cpp
  test_skin_match.cpp)
target_include_directories(sns_reference_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_link_libraries(sns_reference_tests PRIVATE
  sns::core sns::mo2 sns::appearance sns::presets nlohmann_json::nlohmann_json sns::compile_options Catch2::Catch2WithMain)
catch_discover_tests(sns_reference_tests TEST_PREFIX "refint:" DISCOVERY_MODE PRE_TEST)

# AG04: the value bridge between QML and the engines' JSON (needs Qt, so only with the desktop app).
if(SNS_BUILD_APP AND WIN32)
  find_package(Qt6 6.11 REQUIRED COMPONENTS Core Qml)
  add_executable(sns_forge_behavior_tests test_json_bridge.cpp)
  target_include_directories(sns_forge_behavior_tests PRIVATE ${PROJECT_SOURCE_DIR}/apps/desktop/viewmodels)
  target_link_libraries(sns_forge_behavior_tests PRIVATE Qt6::Core Qt6::Qml nlohmann_json::nlohmann_json sns::compile_options Catch2::Catch2WithMain)
  catch_discover_tests(sns_forge_behavior_tests TEST_PREFIX "refint:" DISCOVERY_MODE PRE_TEST)

  # AG05: the published worker over its real pipe. The worker is published by hand next to the app (bin/worker); the test skips when it is not.
  add_executable(sns_worker_pipe_tests test_worker_pipe.cpp)
  target_compile_definitions(sns_worker_pipe_tests PRIVATE SNS_WORKER_EXE="${CMAKE_BINARY_DIR}/bin/worker/SkyrimNpcStudio.BethesdaWorker.exe")
  target_link_libraries(sns_worker_pipe_tests PRIVATE sns::ipc sns::creation nlohmann_json::nlohmann_json sns::compile_options Catch2::Catch2WithMain)
  catch_discover_tests(sns_worker_pipe_tests TEST_PREFIX "refint:" DISCOVERY_MODE PRE_TEST)
endif()
