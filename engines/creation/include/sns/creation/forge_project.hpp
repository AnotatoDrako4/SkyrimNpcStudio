#pragma once

#include <filesystem>
#include <string>

#include "sns/projects/command_stack.hpp"
#include "sns/creation/native_npc_draft.hpp"

namespace sns::creation {

inline constexpr int kForgeProjectVersion = 2;

struct ForgeEnvironment {
  std::string instance_name;
  std::string instance_ini;
  std::string profile;
  std::string game_edition;
};

struct ForgeProject {
  int format_version = kForgeProjectVersion;
  std::string id;
  std::string name;
  std::string description;
  NativeNpcDraft npc;
  nlohmann::json export_options = {{"pluginName", "CharacterForge.esp"}, {"eslFlag", false}, {"createZip", true}};
  std::vector<std::string> dependencies;
  ForgeEnvironment environment;
};

// A .sncf/ folder is independent from .snps and never writes into Skyrim or MO2.
class ForgeProjectStore {
 public:
  static ForgeProject create(std::string name, ForgeEnvironment environment = {});
  static ForgeProject load(const std::filesystem::path& directory);
  static void save(const std::filesystem::path& directory, const ForgeProject& project);
};

// Dedicated history and state: never touches the AppearanceDraft of existing NPCs.
class ForgeDraft {
 public:
  void replace(ForgeProject project);
  [[nodiscard]] const ForgeProject& current() const noexcept { return project_; }
  void set_project_name(std::string value);
  void set_character_name(std::string value);
  void set_editor_id(std::string value);
  void set_description(std::string value);
  void set_native_npc(NativeNpcDraft value);
  void set_export_options(nlohmann::json value);
  bool undo() { return history_.undo(); }
  bool redo() { return history_.redo(); }
  [[nodiscard]] bool can_undo() const noexcept { return history_.can_undo(); }
  [[nodiscard]] bool can_redo() const noexcept { return history_.can_redo(); }
  [[nodiscard]] bool dirty() const noexcept { return !history_.is_clean(); }
  void mark_saved() noexcept { history_.mark_clean(); }

 private:
  void set_field(std::string& field, std::string value, std::string label);
  ForgeProject project_;
  projects::CommandStack history_;
};

}  // namespace sns::creation
