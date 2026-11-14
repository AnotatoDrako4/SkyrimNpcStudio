#include "sns/mo2/ingame_test_kit.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <set>

#include <nlohmann/json.hpp>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::mo2 {
namespace {

using Json = nlohmann::json;

constexpr std::uint32_t kMaxFullIndex = 0xFD;   // 0xFE is the light-plugin space, 0xFF the runtime one
constexpr std::uint32_t kMaxLightIndex = 0xFFF;
constexpr std::uint32_t kMaxLightLocal = 0xFFF;
constexpr std::uint32_t kMaxLocal = 0xFFFFFF;
constexpr std::string_view kExportFormat = "skyrim-npc-studio/export";

std::string str(const Json& object, const char* key) {
  const auto it = object.find(key);
  return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

// Safe as an MO2 mod folder, a ZIP name and a console argument (bat takes one bare word).
std::string safe_stem(std::string_view plugin_file) {
  const auto stem = plugin_file.substr(0, plugin_file.rfind('.'));
  std::string out;
  for (const char c : stem) {
    const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    out.push_back(keep ? c : '_');
    if (out.size() == 60) break;
  }
  return out.empty() ? std::string("Mod") : out;
}

std::string upper(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

}  // namespace

std::optional<FormKeyRef> parse_form_key(std::string_view text) {
  const auto colon = text.find(':');
  if (colon == std::string_view::npos || colon == 0 || colon > 8 || colon + 1 >= text.size()) return std::nullopt;
  std::uint32_t id = 0;
  for (const char c : text.substr(0, colon)) {
    int digit = -1;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    if (digit < 0) return std::nullopt;
    id = id * 16 + static_cast<std::uint32_t>(digit);
  }
  if (id > kMaxLocal) return std::nullopt;
  const auto plugin = text.substr(colon + 1);
  if (!text::ends_with_icase(plugin, ".esp") && !text::ends_with_icase(plugin, ".esm") && !text::ends_with_icase(plugin, ".esl"))
    return std::nullopt;
  return FormKeyRef{id, std::string(plugin)};
}

std::string format_form_id(std::uint32_t id) {
  std::array<char, 9> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%08X", id);
  return buffer.data();
}

RuntimeFormId runtime_form_id(const EffectiveLoadOrder& load_order, std::string_view plugin, std::uint32_t local_id) {
  std::uint32_t full = 0, light = 0;
  for (const auto& p : load_order.active) {
    if (!p.path) continue;  // listed but no mod provides the file: the game never loads it, so it takes no index
    if (!p.header) {
      if (text::iequals_ascii(p.name, plugin)) return {std::nullopt, "No se pudo leer la cabecera de " + p.name + "."};
      // Its kind (light or full) is unknown, so every index after it is unknown too.
      return {std::nullopt, "No se pudo leer la cabecera de " + p.name + ", que carga antes de " + std::string(plugin) +
                                ": el índice de carga no es fiable."};
    }
    const bool is_light = p.header->is_light;
    if (text::iequals_ascii(p.name, plugin)) {
      if (is_light) {
        if (local_id > kMaxLightLocal)
          return {std::nullopt, "El id local " + format_form_id(local_id).substr(2) + " no cabe en un plugin ligero (ESL)."};
        if (light > kMaxLightIndex) return {std::nullopt, "Hay más plugins ligeros activos de los que admite el juego."};
        return {0xFE000000u | (light << 12) | local_id, {}};
      }
      if (full > kMaxFullIndex) return {std::nullopt, p.name + " queda fuera del límite de 254 plugins completos."};
      return {(full << 24) | local_id, {}};
    }
    (is_light ? light : full)++;
  }
  return {std::nullopt, std::string(plugin) + " no está activo en este perfil."};
}

ExportManifestSummary parse_export_manifest(std::string_view bytes) {
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB &&
      static_cast<unsigned char>(bytes[2]) == 0xBF)
    bytes.remove_prefix(3);
  const auto json = Json::parse(bytes, nullptr, false);
  if (json.is_discarded() || !json.is_object()) throw Error(ErrorKind::Parse, "manifest.json is not a JSON object");
  ExportManifestSummary out;
  out.format = str(json, "format");
  if (out.format != kExportFormat)
    throw Error(ErrorKind::Parse, "manifest.json is not a Skyrim NPC Studio export (format '" + out.format + "')");
  out.mode = str(json, "mode");
  const auto plugin = json.value("plugin", Json::object());
  if (!plugin.is_object()) throw Error(ErrorKind::Parse, "manifest.json has no plugin");
  out.plugin_file = str(plugin, "fileName");
  out.plugin_sha256 = str(plugin, "sha256");
  const auto esl = plugin.find("eslFlagged");
  out.esl_flagged = esl != plugin.end() && esl->is_boolean() && esl->get<bool>();
  if (out.plugin_file.empty() || out.plugin_sha256.empty())
    throw Error(ErrorKind::Parse, "manifest.json has no plugin file name or SHA-256");
  const auto records = json.value("records", Json::array());
  if (!records.is_array()) throw Error(ErrorKind::Parse, "manifest.json records is not a list");
  for (const auto& r : records) {
    if (!r.is_object()) continue;
    out.records.push_back({str(r, "formKey"), str(r, "recordType"), str(r, "editorId"), str(r, "kind")});
  }
  return out;
}

std::string console_command_for(std::string_view record_type) {
  static const std::set<std::string, std::less<>> place = {"NPC_", "STAT", "MSTT", "FURN", "ACTI", "CONT", "LIGH", "TREE", "FLOR", "DOOR"};
  static const std::set<std::string, std::less<>> add = {"ARMO", "WEAP", "AMMO", "MISC", "BOOK", "ALCH", "INGR", "KEYM", "SLGM", "SCRL"};
  if (place.contains(record_type)) return "player.placeatme";
  if (add.contains(record_type)) return "player.additem";
  return {};
}

InGameTestKit build_ingame_test_kit(const ExportManifestSummary& manifest, const EffectiveLoadOrder& load_order,
                                    const InGameTestKitContext& context) {
  InGameTestKit kit;
  kit.kit_name = "SNS_Test_" + safe_stem(manifest.plugin_file);
  kit.batch_file = kit.kit_name + ".txt";
  kit.report_file = "ACTA_" + kit.kit_name + ".md";

  // The plugin the profile loads must be this export, byte for byte: an older build with the same name would test
  // something else.
  const auto active = std::find_if(load_order.active.begin(), load_order.active.end(),
                                   [&](const ResolvedPlugin& p) { return text::iequals_ascii(p.name, manifest.plugin_file); });
  std::string provider;
  if (active == load_order.active.end()) {
    kit.errors.push_back(manifest.plugin_file + " no está activo en el perfil «" + context.profile +
                         "». Instala el ZIP del mod con «Instalar mod desde archivo» de MO2, actívalo, cierra MO2 y vuelve a "
                         "abrir el perfil en el estudio.");
  } else {
    provider = active->provider;
    if (context.installed_plugin_sha256.empty())
      kit.errors.push_back("No se pudo leer el plugin activo " + manifest.plugin_file + " para compararlo con la exportación.");
    else if (upper(context.installed_plugin_sha256) != upper(manifest.plugin_sha256))
      kit.errors.push_back("El " + manifest.plugin_file + " que carga el perfil (mod «" + provider +
                           "») no es el de esta exportación: su SHA-256 es distinto. Instala el ZIP de esta exportación.");
  }

  std::set<std::string> seen;
  std::string batch;
  for (const auto& record : manifest.records) {
    InGameTestLine line{record, {}, {}, {}};
    const auto key = parse_form_key(record.form_key);
    if (!key) {
      line.note = "FormKey ilegible en el manifiesto.";
      kit.lines.push_back(std::move(line));
      continue;
    }
    if (const auto id = runtime_form_id(load_order, key->plugin, key->local_id); id.id) {
      line.runtime_form_id = format_form_id(*id.id);
      if (const auto verb = console_command_for(record.record_type); !verb.empty()) {
        line.command = verb + " " + line.runtime_form_id + " 1";
        if (seen.insert(line.command).second) batch += line.command + "\r\n";
      } else {
        line.note = "Sin comando de consola para " + record.record_type + ": se comprueba a través de los registros que lo usan.";
      }
    } else {
      line.note = id.error;
    }
    kit.lines.push_back(std::move(line));
  }
  if (batch.empty() && kit.errors.empty())
    kit.errors.push_back("Ningún registro de esta exportación se puede colocar o añadir desde la consola.");
  kit.batch_text = batch;
  kit.ok = kit.errors.empty();

  // Acceptance report: everything needed to repeat the test and tie the observation to these exact bytes.
  std::string r;
  r += "# Acta de prueba en Skyrim — " + kit.kit_name + "\r\n\r\n";
  r += "Generada por Skyrim NPC Studio el " + context.created_utc +
       " (UTC). Rellénala tú dentro del juego; el estudio no la marca como verificada.\r\n\r\n";
  r += "| Dato | Valor |\r\n|---|---|\r\n";
  r += "| Plugin | `" + manifest.plugin_file + "`" + (manifest.esl_flagged ? " (ESL)" : "") + " |\r\n";
  r += "| SHA-256 del plugin exportado | `" + upper(manifest.plugin_sha256) + "` |\r\n";
  r += "| SHA-256 del plugin que carga el perfil | " +
       (context.installed_plugin_sha256.empty() ? std::string("—") : "`" + upper(context.installed_plugin_sha256) + "`") + " |\r\n";
  r += "| Mod de MO2 que lo aporta | " + (provider.empty() ? std::string("—") : provider) + " |\r\n";
  r += "| Perfil de MO2 | " + context.profile + " |\r\n";
  r += "| Modo de exportación | " + manifest.mode + " |\r\n";
  r += "| Carpeta de la exportación | `" + context.export_folder + "` |\r\n\r\n";
  r += "## Cómo ejecutar la prueba\r\n\r\n";
  r += "1. Instala este kit con «Instalar mod desde archivo» de MO2 en el mismo perfil y actívalo.\r\n";
  r += "2. Inicia Skyrim desde MO2 con ese perfil y carga una partida de prueba (no tu partida personal).\r\n";
  r += "3. Opcional: ve a un sitio despejado escribiendo `coc qasmoke` en la consola. No va dentro del archivo: según "
       "UESP, `coc` desde un archivo `bat` cierra el juego.\r\n";
  r += "4. Abre la consola y escribe: `bat " + kit.kit_name + "`\r\n";
  r += "5. Comprueba cada fila, anota lo que ves y haz una captura por fila.\r\n\r\n";
  r += "Si el orden de carga del perfil cambia, los FormID cambian: genera el kit de nuevo.\r\n\r\n";
  r += "## Registros\r\n\r\n| FormKey | Tipo | EditorID | FormID en el juego | Comando | Resultado observado |\r\n"
       "|---|---|---|---|---|---|\r\n";
  for (const auto& line : kit.lines) {
    r += "| `" + line.record.form_key + "` | " + line.record.record_type + " | " + line.record.editor_id + " | " +
         (line.runtime_form_id.empty() ? std::string("—") : "`" + line.runtime_form_id + "`") + " | " +
         (line.command.empty() ? line.note : "`" + line.command + "`") + " | ☐ correcto ☐ incorrecto |\r\n";
  }
  if (!kit.errors.empty()) {
    r += "\r\n## Bloqueos\r\n\r\n";
    for (const auto& e : kit.errors) r += "- " + e + "\r\n";
  }
  r += "\r\n## Resultado\r\n\r\n- Probado por: \r\n- Fecha: \r\n- Capturas: \r\n- Observaciones: \r\n\r\n";
  r += "inGameVerified: false  (cámbialo a true solo tras observarlo en el juego)\r\n";
  kit.report_text = r;
  return kit;
}

}  // namespace sns::mo2
