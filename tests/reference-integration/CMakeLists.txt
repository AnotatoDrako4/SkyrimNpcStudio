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
