#include "sns/creation/forge_project.hpp"

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

class SetField final : public projects::Command {
 public:
  SetField(std::string& target, std::string next, std::string label)
      : target_(target), before_(target), after_(std::move(next)), label_(std::move(label)) {}
  void apply() override { target_ = after_; }
  void revert() override { target_ = before_; }
  [[nodiscard]] std::string description() const override { return label_; }

 private:
  std::string& target_;
  std::string before_;
  std::string after_;
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
  check_text(project.character_name, 160, "Character name");
  check_text(project.description, 4096, "Description");
  check_text(project.editor_id, 128, "EditorID");
  if (!std::all_of(project.editor_id.begin(), project.editor_id.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_';
      }))
    throw Error(ErrorKind::InvalidArgument, "EditorID must contain ASCII letters, digits or underscore only");
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
      {"character", {{"name", project.character_name}, {"editorId", project.editor_id}}},
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
    if (j.value("formatVersion", 0) != kForgeProjectVersion)
      throw Error(ErrorKind::Parse, "Unsupported .sncf project version; do not modify it");
    ForgeProject project;
    project.format_version = kForgeProjectVersion;
    project.id = j.at("id").get<std::string>();
    project.name = j.value("name", "");
    project.description = j.value("description", "");
    const auto character = j.value("character", Json::object());
    project.character_name = character.value("name", "");
    project.editor_id = character.value("editorId", "");
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
  history_.push(std::make_unique<SetField>(field, std::move(value), std::move(label)));
}

void ForgeDraft::set_project_name(std::string value) {
  check_text(value, 160, "Project name");
  set_field(project_.name, std::move(value), "Nombre del proyecto");
}

void ForgeDraft::set_character_name(std::string value) {
  check_text(value, 160, "Character name");
  set_field(project_.character_name, std::move(value), "Nombre del personaje");
}

void ForgeDraft::set_editor_id(std::string value) {
  check_text(value, 128, "EditorID");
  if (!std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_';
      })) throw Error(ErrorKind::InvalidArgument, "EditorID must contain ASCII letters, digits or underscore only");
  set_field(project_.editor_id, std::move(value), "EditorID del personaje");
}

void ForgeDraft::set_description(std::string value) {
  check_text(value, 4096, "Description");
  set_field(project_.description, std::move(value), "Descripcion del proyecto");
}

}  // namespace sns::creation
