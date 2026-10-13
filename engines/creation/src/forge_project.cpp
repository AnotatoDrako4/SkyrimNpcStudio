#include "sns/creation/forge_project.hpp"
#include "sns/creation/forge_workspace.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <random>
#include <sstream>
#include <utility>
#include <nlohmann/json.hpp>

#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"

namespace sns::creation {
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

template<class T> class SetField final : public projects::Command {
 public:
  SetField(T& target, T next, std::string label)
      : target_(target), before_(target), after_(std::move(next)), label_(std::move(label)) {}
  void apply() override { target_ = after_; }
  void revert() override { target_ = before_; }
  [[nodiscard]] std::string description() const override { return label_; }

 private:
  T& target_;
  T before_;
  T after_;
  std::string label_;
};

void check_text(const std::string& value, std::size_t limit, const char* field) {
  if (value.size() > limit) throw Error(ErrorKind::InvalidArgument, std::string(field) + " exceeds the permitted length");
}

void validate(const ForgeProject& project) {
  if (project.format_version != kForgeProjectVersion)
    throw Error(ErrorKind::InvalidArgument, "Unsupported .sncf project format version");
  if (project.id.empty()) throw Error(ErrorKind::InvalidArgument, "Missing Character Forge project id");
  check_text(project.name, 160, "Project name");
  check_text(project.description, 4096, "Description");
  validate_native_draft(project.npc);
  validate_export_options(project.export_options);
  validate_forge_workspace(project.workspace);

}

void ensure_directory(const fs::path& directory) {
  if (directory.extension() != ".sncf")
    throw Error(ErrorKind::InvalidArgument, "Character Forge projects must use a .sncf directory");
}

std::string make_id() {
  std::random_device seed;
  std::mt19937_64 generator(seed());
  std::ostringstream stream;
  stream << std::hex << generator() << generator();
  return stream.str();
}

}  // namespace

ForgeProject ForgeProjectStore::create(std::string name, ForgeEnvironment environment) {
  ForgeProject project;
  project.id = make_id();
  project.name = name.empty() ? "Nuevo personaje" : std::move(name);
  project.environment = std::move(environment);
  validate(project);
  return project;
}

void ForgeProjectStore::save(const fs::path& directory, const ForgeProject& project) {
  ensure_directory(directory);
  validate(project);
  const Json j{
      {"formatVersion", project.format_version},
      {"kind", "characterForge"},
      {"id", project.id},
      {"name", project.name},
      {"description", project.description},
      {"nativeNpc", native_npc_to_json(project.npc)},
      {"exportOptions", project.export_options},
      {"dependencies", project.dependencies},
      {"workspace", project.workspace},
      {"environment", {{"instanceName", project.environment.instance_name},
                       {"instanceIni", project.environment.instance_ini},
                       {"profile", project.environment.profile},
                       {"gameEdition", project.environment.game_edition}}}};
  std::error_code error;
  // Never overwrite an unrelated Forge project merely because its folder has the same name.
  if (fs::exists(directory / "project.json")) {
    const auto existing = load(directory);
    if (existing.id != project.id)
      throw Error(ErrorKind::InvalidArgument, "Destination already contains a different Character Forge project");
  }
  fs::create_directories(directory / "assets", error);
  if (error) throw Error(ErrorKind::Io, "Cannot create Character Forge asset directory: " + error.message());
  file_io::write_file_atomic(directory / "project.json", j.dump(2));
}

ForgeProject ForgeProjectStore::load(const fs::path& directory) {
  ensure_directory(directory);
  try {
    const auto json = file_io::read_text_file(directory / "project.json", 1024u * 1024u);
    const Json j = Json::parse(json);
    if (!j.is_object() || j.value("kind", "") != "characterForge")
      throw Error(ErrorKind::Parse, "Not a Character Forge project");
    if (!j.contains("formatVersion") || !j.at("formatVersion").is_number_integer() ||
        (j.at("formatVersion") != 1 && j.at("formatVersion") != kForgeProjectVersion))
      throw Error(ErrorKind::Parse, "Unsupported .sncf project version; do not modify it");
    ForgeProject project;
    project.format_version = kForgeProjectVersion;
    project.id = j.at("id").get<std::string>();
    project.name = j.value("name", "");
    project.description = j.value("description", "");
    if (j.at("formatVersion").get<int>() == 1) {
      const auto character = j.value("character", Json::object());
      project.npc.name = character.value("name", "");
      project.npc.editor_id = character.value("editorId", "");
    } else {
      project.npc = native_npc_from_json(j.at("nativeNpc"));
      project.export_options = j.value("exportOptions", project.export_options);
      project.dependencies = j.value("dependencies", std::vector<std::string>{});
      project.workspace = j.value("workspace", Json::object());
    }
    const auto environment = j.value("environment", Json::object());
    project.environment = {environment.value("instanceName", ""), environment.value("instanceIni", ""),
                           environment.value("profile", ""), environment.value("gameEdition", "")};
    validate(project);
    return project;
  } catch (const nlohmann::json::exception& ex) {
    throw Error(ErrorKind::Parse, std::string("Invalid Character Forge project JSON: ") + ex.what());
  }
}

void ForgeDraft::replace(ForgeProject project) {
  history_.clear();
  project_ = std::move(project);
  history_.mark_clean();
}

void ForgeDraft::set_field(std::string& field, std::string value, std::string label) {
  if (field == value) return;
  history_.push(std::make_unique<SetField<std::string>>(field, std::move(value), std::move(label)));
}

void ForgeDraft::set_project_name(std::string value) {
  check_text(value, 160, "Project name");
  set_field(project_.name, std::move(value), "Nombre del proyecto");
}

void ForgeDraft::set_character_name(std::string value) {
  check_text(value, 160, "Character name");
  set_field(project_.npc.name, std::move(value), "Nombre del personaje");
}

void ForgeDraft::set_editor_id(std::string value) {
  check_text(value, 128, "EditorID");
  auto candidate = project_.npc;
  candidate.editor_id = value;
  validate_native_draft(candidate);
  set_field(project_.npc.editor_id, std::move(value), "EditorID del personaje");
}

void ForgeDraft::set_description(std::string value) {
  check_text(value, 4096, "Description");
  set_field(project_.description, std::move(value), "Descripcion del proyecto");
}

void ForgeDraft::set_native_npc(NativeNpcDraft value) {
  validate_native_draft(value);
  if (native_npc_to_json(project_.npc) == native_npc_to_json(value)) return;
  history_.push(std::make_unique<SetField<NativeNpcDraft>>(project_.npc, std::move(value), "NPC nuevo"));
}
void ForgeDraft::set_export_options(Json value) {
  validate_export_options(value);
  if (project_.export_options == value) return;
  history_.push(std::make_unique<SetField<Json>>(project_.export_options, std::move(value), "Opciones de exportación"));
}
void ForgeDraft::set_workspace(Json value) {
  validate_forge_workspace(value);
  if (project_.workspace == value) return;
  history_.push(std::make_unique<SetField<Json>>(project_.workspace, std::move(value), "Preparación del workspace"));
}
}  // namespace sns::creation
