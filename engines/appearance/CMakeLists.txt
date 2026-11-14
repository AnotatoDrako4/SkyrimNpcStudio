add_library(sns_appearance STATIC
  src/integrity_guard.cpp
  src/appearance_draft.cpp
  src/compatibility_guardian.cpp
  src/control_map.cpp
  src/easy_mod.cpp
  src/face_source_index.cpp
  src/face_transfer.cpp
  src/facegen_smp_preservation.cpp
  src/fsmp_preflight.cpp
  src/hair_graph.cpp
  src/hair_regression_guard.cpp
  src/hair_safe_recovery.cpp
  src/npc_audit.cpp
  src/npc_runtime_inspector.cpp
  src/review_case.cpp
  src/scan_completeness.cpp
  src/skin_match.cpp
  src/source_matrix.cpp
  src/source_draft.cpp
  src/ube_smp_validator.cpp)
add_library(sns::appearance ALIAS sns_appearance)
target_include_directories(sns_appearance PUBLIC include)
target_link_libraries(sns_appearance
  PUBLIC sns::projects sns::outfit sns::face sns::nif sns::assets nlohmann_json::nlohmann_json
  PRIVATE bcrypt
  PRIVATE sns::compile_options)
