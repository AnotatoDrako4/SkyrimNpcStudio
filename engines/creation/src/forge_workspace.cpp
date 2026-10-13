#include "sns/creation/forge_workspace.hpp"
#include "sns/core/error.hpp"
#include <cmath>

namespace sns::creation {
using Json = nlohmann::json;
namespace {
void require(bool valid) {
  if (!valid) throw Error(ErrorKind::Parse, "Invalid Character Forge workspace schema");
}
void text(const Json& object, const char* key, std::size_t limit = 4096) {
  if (object.contains(key)) require(object.at(key).is_string() && object.at(key).get_ref<const std::string&>().size() <= limit);
}
void model(const Json& entry) {
  require(entry.is_object());
  for (const auto* key : {"name", "path", "format"})
    require(entry.contains(key) && entry.at(key).is_string() && !entry.at(key).get_ref<const std::string&>().empty());
  for (const auto* key : {"name", "path", "format", "status"}) text(entry, key);
  for (const auto* key : {"sizeBytes", "meshes", "materials", "textures", "skins"})
    if (entry.contains(key)) require(entry.at(key).is_number_integer() && entry.at(key) >= 0 && entry.at(key) <= 2147483648ULL);
  for (const auto* key : {"convertedToNif", "metadataInspected"}) if (entry.contains(key)) require(entry.at(key).is_boolean());
}
}
void validate_forge_workspace(const Json& w) {
  require(w.is_object() && w.dump().size() <= 256 * 1024);
  text(w, "tool", 32);
  if (w.contains("model")) model(w.at("model"));
  if (w.contains("library")) {
    require(w.at("library").is_array() && w.at("library").size() <= 100);
    for (const auto& entry : w.at("library")) model(entry);
  }
  for (const auto* key : {"blender", "ai", "materials", "recordLabels", "exportWizard"})
    if (w.contains(key)) require(w.at(key).is_object());
  if (w.contains("blender")) { text(w.at("blender"), "executable"); text(w.at("blender"), "status", 64); }
  if (w.contains("ai")) {
    const auto& ai = w.at("ai"); text(ai, "description"); text(ai, "provider", 32);
    for (const auto* key : {"front", "side", "back"}) if (ai.contains(key)) {
      const auto& image = ai.at(key); require(image.is_object()); text(image, "path");
      for (const auto* dimension : {"width", "height"})
        if (image.contains(dimension)) require(image.at(dimension).is_number_integer() && image.at(dimension) > 0 && image.at(dimension) < 100000);
    }
  }
  if (w.contains("materials")) {
    const auto& m = w.at("materials");
    for (const auto* key : {"name", "diffuse", "normal", "alpha"}) text(m, key);
    if (m.contains("opacity")) require(m.at("opacity").is_number() && std::isfinite(m.at("opacity").get<double>()) && m.at("opacity") >= 0 && m.at("opacity") <= 1);
  }
  if (w.contains("recordLabels")) for (const auto& [key, value] : w.at("recordLabels").items())
    require(key.size() <= 256 && value.is_string() && value.get_ref<const std::string&>().size() <= 256);
  if (w.contains("exportWizard")) {
    const auto& e = w.at("exportWizard"); text(e, "outputFolder");
    if (e.contains("step")) require(e.at("step").is_number_integer() && e.at("step") >= 0 && e.at("step") <= 4);
  }
}
}  // namespace sns::creation
