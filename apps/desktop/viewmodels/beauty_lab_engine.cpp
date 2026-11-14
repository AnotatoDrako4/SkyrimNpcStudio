#include "beauty_lab_engine.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QVector3D>
#include <algorithm>
#include <cmath>

#include "json_bridge.hpp"
#include "sns/core/error.hpp"
#include "sns/core/file_io.hpp"
#include "sns/core/text.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/presets/jslot_parser.hpp"

namespace sns::app {
namespace {

constexpr const char* kVirtualBaldFormKey = "000000:BALD";

QColor rgb_to_qcolor(const face::Rgb8& c) { return QColor(c.r, c.g, c.b); }
face::Rgb8 qcolor_to_rgb(const QColor& c) {
  return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()), static_cast<std::uint8_t>(c.blue())};
}

QString now_iso() {
  const auto now = QDateTime::currentDateTimeUtc();
  return now.toString(Qt::ISODateWithMs);
}

QString normalize_mesh_path(const QString& raw) {
  QString norm = raw.trimmed();
  if (norm.isEmpty()) return {};
  norm.replace('/', '\\');
  while (norm.startsWith('\\')) norm.remove(0, 1);
  if (norm.startsWith(QStringLiteral("data\\"), Qt::CaseInsensitive)) norm.remove(0, 5);
  while (norm.startsWith('\\')) norm.remove(0, 1);
  if (!norm.startsWith(QStringLiteral("meshes\\"), Qt::CaseInsensitive)) norm = QStringLiteral("meshes\\") + norm;
  return norm;
}

QString classify_package(const QString& plugin, const QString& model_path, const QString& editor_id) {
  const QString combined = (plugin + " " + model_path + " " + editor_id).toLower();
  if (combined.contains("kshair") || combined.contains("ks hair")) return QStringLiteral("KS Hairdos");
  if (combined.contains("apachii")) return QStringLiteral("ApachiiSkyHair");
  if (combined.contains("hghair") || combined.contains("hg hair")) return QStringLiteral("HG Hairdos");
  if (combined.contains("yundao") || combined.contains("dint999")) return QStringLiteral("HDT-SMP Hair Pack");
  if (combined.contains("highpolyhead") || combined.contains("kl_highpoly")) return QStringLiteral("High Poly Head");
  if (combined.contains("eyesofbeauty") || combined.contains("theeyesofbeauty")) return QStringLiteral("The Eyes of Beauty");
  if (combined.contains("brows.esp") || combined.contains("hvergelmir")) return QStringLiteral("Hvergelmir Brows / Beards");
  if (combined.contains("beards.esp")) return QStringLiteral("Beards");
  static const QStringList vanilla_masters = {"skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm"};
  if (vanilla_masters.contains(plugin.toLower()) || plugin.toLower().startsWith("cc")) return QStringLiteral("Vanilla / Creation Club");
  return plugin.isEmpty() ? QStringLiteral("Otro") : plugin;
}

QString classify_eye_color(const QString& name, const QString& editor_id, const QString& model_path) {
  const QString c = (name + " " + editor_id + " " + model_path).toLower();
  if (c.contains("vampire") || c.contains("vamp")) return QStringLiteral("Vampiro / Rojo");
  if (c.contains("blue") || c.contains("azul") || c.contains("ice") || c.contains("cyan")) return QStringLiteral("Azul");
  if (c.contains("green") || c.contains("verde") || c.contains("hazel")) return QStringLiteral("Verde");
  if (c.contains("brown") || c.contains("marr") || c.contains("dark")) return QStringLiteral("Marrón");
  if (c.contains("amber") || c.contains("yellow") || c.contains("gold") || c.contains("orange")) return QStringLiteral("Ámbar / Dorado");
  if (c.contains("grey") || c.contains("gray") || c.contains("silver") || c.contains("blind") || c.contains("white")) return QStringLiteral("Gris / Plata");
  if (c.contains("red") || c.contains("demon") || c.contains("blood")) return QStringLiteral("Rojo");
  return QStringLiteral("Estándar");
}

QString head_part_type_es(const QString& type) {
  if (type == QLatin1String("Hair")) return QStringLiteral("Cabello");
  if (type == QLatin1String("Eyes")) return QStringLiteral("Ojos");
  if (type == QLatin1String("Eyebrows")) return QStringLiteral("Cejas");
  if (type == QLatin1String("FacialHair")) return QStringLiteral("Vello facial / Barba");
  if (type == QLatin1String("Scar")) return QStringLiteral("Cicatrices / Marcas");
  if (type == QLatin1String("Face")) return QStringLiteral("Cabeza base (Face)");
  return type;
}

QString plugin_provider_of(const Mo2Snapshot* snapshot, const std::string& plugin) {
  if (!snapshot || plugin.empty()) return {};
  const auto it = snapshot->plugin_provider.find(text::to_lower_ascii(plugin));
  return it != snapshot->plugin_provider.end() ? qs(it->second) : QString();
}

bool plugin_is_active(const Mo2Snapshot* snapshot, const std::string& plugin) {
  if (!snapshot || plugin.empty()) return false;
  return std::any_of(snapshot->load_order.active.begin(), snapshot->load_order.active.end(),
                     [&](const auto& p) { return text::iequals_ascii(p.name, plugin); });
}

QVariantMap locate_asset_summary(const Mo2Snapshot* snapshot, const assets::AssetResolver* resolver, const std::string& data_path) {
  QVariantMap out{{"path", qs(data_path)}, {"found", false}, {"kind", "missing"}, {"provider", QString()}, {"overrideCount", 0}};
  if (data_path.empty()) {
    out["found"] = true;
    out["kind"] = QStringLiteral("none");
    return out;
  }
  QStringList overridden_by;
  if (snapshot) {
    for (const auto idx : snapshot->vfs.providers_of(data_path)) {
      overridden_by << qs(snapshot->vfs.providers()[idx].name);
    }
  }
  out["overriddenBy"] = overridden_by;
  out["overrideCount"] = std::max(0, static_cast<int>(overridden_by.size()) - 1);
  if (resolver) {
    if (const auto loc = resolver->locate(data_path)) {
      out["found"] = true;
      out["kind"] = loc->kind == assets::AssetLocation::Kind::Loose ? QStringLiteral("loose") : QStringLiteral("bsa");
      out["provider"] = qs(loc->kind == assets::AssetLocation::Kind::Archive && !loc->archive_provider.empty()
                               ? loc->archive_provider
                               : loc->provider);
      out["archiveName"] = loc->kind == assets::AssetLocation::Kind::Archive ? qs(loc->provider) : QString();
    }
  } else if (!overridden_by.isEmpty()) {
    out["found"] = true;
    out["kind"] = QStringLiteral("loose");
    out["provider"] = overridden_by.last();
  }
  return out;
}

bool is_op_blocked(const QVariantMap& face_compatibility, const QString& op) {
  for (const auto& b : face_compatibility.value("blocked").toList()) {
    if (b.toMap().value("op").toString() == op) return true;
  }
  return false;
}

QString blocked_reason_for(const QVariantMap& face_compatibility, const QString& op) {
  for (const auto& b : face_compatibility.value("blocked").toList()) {
    const auto m = b.toMap();
    if (m.value("op").toString() == op) return m.value("reason").toString();
  }
  return {};
}

}  // namespace

QVariantMap inspect_head_part_asset(const QVariantMap& head_part_option,
                                    const face::FaceRecipe* recipe,
                                    const Mo2Snapshot* snapshot,
                                    const assets::AssetResolver* resolver,
                                    face::FaceAssetCache* face_assets) {
  const QString form_key = head_part_option.value("formKey").toString();
  const QString editor_id = head_part_option.value("editorId").toString();
  const QString name = head_part_option.value("name").toString().isEmpty() ? editor_id : head_part_option.value("name").toString();
  const QString type = head_part_option.value("type").toString();
  const QString plugin = head_part_option.value("plugin").toString();
  const QString raw_model =
      head_part_option.value("meshPath", head_part_option.value("modelPath", head_part_option.value("model"))).toString();
  const QString norm_model = normalize_mesh_path(raw_model);

  const QString plugin_mod = plugin_provider_of(snapshot, ss(plugin));
  const bool plugin_active = snapshot ? plugin_is_active(snapshot, ss(plugin)) : true;
  const bool is_bald = type == QLatin1String("Hair") && norm_model.isEmpty();
  const bool is_none = norm_model.isEmpty();

  QVariantMap result{
      {"formKey", form_key},
      {"editorId", editor_id},
      {"name", name},
      {"type", type},
      {"typeLabel", head_part_type_es(type)},
      {"plugin", plugin},
      {"pluginMod", plugin_mod},
      {"pluginActive", plugin_active},
      {"packageFamily", classify_package(plugin, norm_model, editor_id)},
      {"modelPath", norm_model},
      {"isBaldPart", is_bald},
      {"isNonePart", is_none},
      {"requiresFaceGenRebuild", true},
      {"rebuildNotice", QObject::tr("Cambiar un HeadPart (PNAM) requiere regenerar el archivo FaceGen NIF del NPC. "
                                    "En Skyrim SE/AE el peinado y la geometría facial van horneados en FaceGeom.")}};

  if (type == QLatin1String("Eyes")) {
    result["eyeColorGroup"] = classify_eye_color(name, editor_id, norm_model);
  }

  QStringList warnings, errors;
  if (!plugin.isEmpty() && snapshot && !plugin_active) {
    errors << QObject::tr("El plugin propietario «%1» no está activo en el orden de carga.").arg(plugin);
  }

  if (is_none) {
    result["meshFound"] = true;
    result["assetKind"] = QStringLiteral("none");
    result["effectiveProvider"] = plugin_mod;
    result["overrideCount"] = 0;
    result["overriddenBy"] = QStringList{};
    result["textures"] = QVariantList{};
    result["missingTextures"] = QStringList{};
    result["tris"] = QVariantList{};
    result["vertexCount"] = 0;
    result["triangleCount"] = 0;
    result["shapeCount"] = 0;
    result["hasSmp"] = false;
    result["valid"] = errors.isEmpty();
    result["status"] = is_bald ? QStringLiteral("bald") : QStringLiteral("valid");
    result["warnings"] = warnings;
    result["errors"] = errors;
    return result;
  }

  const auto mesh_loc = locate_asset_summary(snapshot, resolver, ss(norm_model));
  const bool mesh_found = mesh_loc.value("found").toBool();
  result["meshFound"] = mesh_found;
  result["assetKind"] = mesh_loc.value("kind");
  result["effectiveProvider"] = mesh_loc.value("provider");
  result["overrideCount"] = mesh_loc.value("overrideCount");
  result["overriddenBy"] = mesh_loc.value("overriddenBy");

  if (!mesh_found) {
    errors << QObject::tr("Falta la malla NIF en el perfil MO2: %1").arg(norm_model);
  }

  QVariantList textures_list;
  QStringList missing_textures;
  QVariantList tris_list;
  int total_vertices = 0;
  int total_triangles = 0;
  int shape_count = 0;
  bool supports_hair_tint = false;
  bool has_smp = false;
  QString smp_xml_path;
  bool smp_xml_found = false;
  QString shader_summary;

  if (mesh_found && resolver) {
    if (const auto bytes = resolver->read(ss(norm_model))) {
      try {
        const auto mesh = nif::load_mesh(*bytes, ss(norm_model));
        shape_count = static_cast<int>(mesh.shapes.size());
        if (!mesh.physics_xml.empty()) {
          has_smp = true;
          smp_xml_path = qs(mesh.physics_xml);
          smp_xml_found = resolver->locate(mesh.physics_xml).has_value();
          if (!smp_xml_found) {
            warnings << QObject::tr("Definición física HDT-SMP no encontrada: %1").arg(smp_xml_path);
          }
        }
        static const char* const kSlotLabels[] = {
            "Diffuse", "Normal", "Subsurface / Skin", "Height / Detail", "Environment",
            "Env Mask", "Tint", "Specular / Backlight", "Slot 8"};
        QStringList seen_textures;
        for (const auto& s : mesh.shapes) {
          if (s.hidden) continue;
          total_vertices += static_cast<int>(s.positions.size());
          total_triangles += static_cast<int>(s.indices.size() / 3);
          if (s.shader_type == nif::kShaderHairTint) supports_hair_tint = true;
          if (shader_summary.isEmpty() && !s.shader_block.empty()) {
            shader_summary = qs(s.shader_block) + QStringLiteral(" (tipo %1)").arg(s.shader_type);
          }
          for (std::size_t slot = 0; slot < s.textures.size(); ++slot) {
            const auto& tex = s.textures[slot];
            if (tex.empty()) continue;
            const QString tex_q = qs(tex);
            if (seen_textures.contains(tex_q, Qt::CaseInsensitive)) continue;
            seen_textures << tex_q;
            auto tex_loc = locate_asset_summary(snapshot, resolver, tex);
            tex_loc["slot"] = static_cast<int>(slot);
            tex_loc["slotLabel"] = QString::fromLatin1(kSlotLabels[slot]);
            textures_list.push_back(tex_loc);
            if (!tex_loc.value("found").toBool()) {
              missing_textures << tex_q;
            }
          }
        }
        for (const auto& w : mesh.warnings) warnings << qs(w);
      } catch (const std::exception& e) {
        errors << QObject::tr("La malla NIF está dañada o no es legible (%1): %2").arg(norm_model, qs(e.what()));
      }
    }
  }

  // Also check companion .xml if not declared in extra data
  if (!has_smp && norm_model.endsWith(QStringLiteral(".nif"), Qt::CaseInsensitive) && resolver) {
    const QString cand = norm_model.left(norm_model.size() - 4) + QStringLiteral(".xml");
    if (resolver->locate(ss(cand)).has_value()) {
      has_smp = true;
      smp_xml_path = cand;
      smp_xml_found = true;
    }
  }

  // If this HeadPart is currently active in `recipe`, inspect its TextureSet record overrides & TRI morph files.
  if (recipe) {
    for (const auto& p : recipe->parts) {
      if (qs(p.form_key).compare(form_key, Qt::CaseInsensitive) != 0 &&
          qs(p.editor_id).compare(editor_id, Qt::CaseInsensitive) != 0) {
        continue;
      }
      if (!p.valid_for_race) {
        warnings << QObject::tr("La pieza «%1» no incluye la raza %2 en su FormList de razas válidas.")
                        .arg(editor_id, qs(recipe->race_editor_id));
      }
      if (p.valid_sex != "any" && (p.valid_sex == "female") != recipe->female) {
        warnings << QObject::tr("La pieza «%1» está marcada para el sexo opuesto.").arg(editor_id);
      }
      if (p.texture_set) {
        for (std::size_t slot = 0; slot < p.texture_set->size(); ++slot) {
          const auto& tex = (*p.texture_set)[slot];
          if (tex.empty()) continue;
          const QString tex_q = qs(tex);
          auto tex_loc = locate_asset_summary(snapshot, resolver, tex);
          tex_loc["slot"] = static_cast<int>(slot);
          tex_loc["slotLabel"] = QStringLiteral("TXST Slot %1").arg(slot);
          textures_list.push_back(tex_loc);
          if (!tex_loc.value("found").toBool() && !missing_textures.contains(tex_q, Qt::CaseInsensitive)) {
            missing_textures << tex_q;
          }
        }
      }
      for (const auto& tri_ref : p.tris) {
        auto tri_loc = locate_asset_summary(snapshot, resolver, tri_ref.path);
        const QString kind_label = tri_ref.kind == face::TriKind::Race      ? QStringLiteral("Race")
                                   : tri_ref.kind == face::TriKind::Chargen ? QStringLiteral("Chargen")
                                                                            : QStringLiteral("Expression");
        tri_loc["triKind"] = kind_label;
        if (!tri_loc.value("found").toBool()) {
          if (tri_ref.kind != face::TriKind::Expression) {
            warnings << QObject::tr("Archivo de morph TRI ausente (%1): %2").arg(kind_label, qs(tri_ref.path));
          }
        } else if (face_assets) {
          try {
            if (const auto parsed = face_assets->tri(tri_ref.path)) {
              tri_loc["vertexCount"] = static_cast<int>(parsed->vertex_count);
              tri_loc["morphCount"] = static_cast<int>(parsed->morphs.size());
            }
          } catch (const std::exception& e) {
            warnings << QObject::tr("Archivo TRI ilegible (%1): %2").arg(qs(tri_ref.path), qs(e.what()));
          }
        }
        tris_list.push_back(tri_loc);
      }
      break;
    }
  }

  if (!missing_textures.isEmpty()) {
    errors << QObject::tr("Faltan %1 textura(s) DDS requeridas por la pieza: %2")
                  .arg(missing_textures.size())
                  .arg(missing_textures.join(QStringLiteral(", ")));
  }

  if ((type == QLatin1String("Hair") || type == QLatin1String("Eyebrows") || type == QLatin1String("FacialHair")) &&
      mesh_found && shape_count > 0 && !supports_hair_tint) {
    warnings << QObject::tr("El shader de la malla no es HairTint: el color de cabello del registro no teñirá esta pieza.");
  }

  const bool valid = errors.isEmpty() && mesh_found && plugin_active;
  const QString status = !plugin_active               ? QStringLiteral("missing-dependency")
                         : !mesh_found                ? QStringLiteral("missing-mesh")
                         : !missing_textures.isEmpty() ? QStringLiteral("missing-textures")
                         : !errors.isEmpty()          ? QStringLiteral("unreadable-mesh")
                                                      : QStringLiteral("valid");
  const QString compat_state = (!plugin_active || !mesh_found) ? QStringLiteral("error")
                               : (!missing_textures.isEmpty() || !warnings.isEmpty()) ? QStringLiteral("warning")
                                                                                      : QStringLiteral("compatible");

  result["valid"] = valid;
  result["status"] = status;
  result["compatibilityState"] = compat_state;
  result["nifReadable"] = mesh_found && errors.isEmpty();
  result["faceGenNotice"] = result.value("rebuildNotice");
  result["shapeCount"] = shape_count;
  result["vertexCount"] = total_vertices;
  result["triangleCount"] = total_triangles;
  result["shaderSummary"] = shader_summary;
  result["supportsHairTint"] = supports_hair_tint;
  result["hasSmp"] = has_smp;
  result["smpXmlPath"] = smp_xml_path;
  result["smpXmlFound"] = smp_xml_found;
  result["textures"] = textures_list;
  result["missingTextures"] = missing_textures;
  result["tris"] = tris_list;
  result["warnings"] = warnings;
  result["errors"] = errors;
  return result;
}

QVariantList filter_head_part_options(const QVariantList& options,
                                      const QString& category,
                                      const QString& search_text,
                                      const QString& package_filter,
                                      const QString& compatibility_filter,
                                      const QStringList& favorite_form_keys) {
  QVariantList out;
  const QString cat = category.trimmed();
  const QString q = search_text.trimmed();
  const QString pkg = package_filter.trimmed();
  const QString compat = compatibility_filter.trimmed();

  for (const auto& v : options) {
    auto item = v.toMap();
    const QString type = item.value("type").toString();
    if (!cat.isEmpty() && cat != QLatin1String("all") && type.compare(cat, Qt::CaseInsensitive) != 0) {
      continue;
    }
    const QString fk = item.value("formKey").toString();
    const QString editor_id = item.value("editorId").toString();
    const QString name = item.value("name").toString();
    const QString plugin = item.value("plugin").toString();
    const QString model = item.value("modelPath").toString();
    const QString item_pkg = item.contains("packageFamily") ? item.value("packageFamily").toString()
                                                            : classify_package(plugin, model, editor_id);
    item["packageFamily"] = item_pkg;
    const bool is_fav = favorite_form_keys.contains(fk, Qt::CaseInsensitive);
    item["isFavorite"] = is_fav;
    if (type == QLatin1String("Eyes") && !item.contains("eyeColorGroup")) {
      item["eyeColorGroup"] = classify_eye_color(name, editor_id, model);
    }

    if (!pkg.isEmpty() && pkg != QLatin1String("all") &&
        item_pkg.compare(pkg, Qt::CaseInsensitive) != 0 &&
        plugin.compare(pkg, Qt::CaseInsensitive) != 0) {
      continue;
    }

    const bool mesh_found = item.value("meshFound", true).toBool();
    const bool has_smp = item.value("hasSmp", false).toBool();
    const bool is_bald = item.value("isBaldPart", false).toBool();
    const int overrides = item.value("overrideCount", 0).toInt();

    if (compat == QLatin1String("compatible-only") && !mesh_found) continue;
    if (compat == QLatin1String("missing-assets") && mesh_found) continue;
    if (compat == QLatin1String("smp-only") && !has_smp) continue;
    if (compat == QLatin1String("overridden") && overrides <= 0) continue;
    if (compat == QLatin1String("favorites-only") && !is_fav) continue;
    if (compat == QLatin1String("bald-only") && !is_bald) continue;

    if (!q.isEmpty()) {
      const bool match = name.contains(q, Qt::CaseInsensitive) ||
                         editor_id.contains(q, Qt::CaseInsensitive) ||
                         fk.contains(q, Qt::CaseInsensitive) ||
                         plugin.contains(q, Qt::CaseInsensitive) ||
                         item_pkg.contains(q, Qt::CaseInsensitive) ||
                         item.value("effectiveProvider").toString().contains(q, Qt::CaseInsensitive);
      if (!match) continue;
    }

    out.push_back(item);
  }
  return out;
}

QVariantMap evaluate_workshop_capabilities(const face::FaceRecipe* recipe,
                                           const QVariantMap& face_compatibility,
                                           const QString& geometry_mode) {
  const bool morphs_blocked = is_op_blocked(face_compatibility, QStringLiteral("face-morphs"));
  const bool head_parts_blocked = is_op_blocked(face_compatibility, QStringLiteral("head-parts"));
  const bool skin_tone_blocked = is_op_blocked(face_compatibility, QStringLiteral("skin-tone"));
  const bool hair_color_blocked = is_op_blocked(face_compatibility, QStringLiteral("hair-color"));
  const bool regen_blocked = is_op_blocked(face_compatibility, QStringLiteral("regenerate"));

  const QString compat_code = face_compatibility.value("code").toString();
  QString parametric_mode = QStringLiteral("parametric-regenerate");
  QString parametric_summary = QObject::tr("Morfología paramétrica editable: los sliders NAM9, presets raciales NAMA y el peso deforman los archivos TRI de la raza con límites técnicos seguros [-1.0, +1.0].");

  if (morphs_blocked) {
    parametric_mode = QStringLiteral("imported-locked");
    parametric_summary = QObject::tr("Geometría personalizada importada (no paramétrica): el FaceGen de este NPC tiene una topología/esculpido incompatible con los archivos TRI de la raza (%1). Los sliders paramétricos están bloqueados para evitar corromper la cabeza.")
                             .arg(blocked_reason_for(face_compatibility, QStringLiteral("face-morphs")));
  } else if (geometry_mode == QLatin1String("preserve")) {
    parametric_mode = QStringLiteral("parametric-preserve-delta");
    parametric_summary = QObject::tr("Edición diferencial sobre FaceGen existente: se conserva el esculpido base del FaceGen y los sliders compatibles se aplican como delta geométrico.");
  }

  return {
      {"loaded", recipe != nullptr},
      {"raceEditorId", recipe ? qs(recipe->race_editor_id) : QString()},
      {"morphRace", recipe ? qs(recipe->morph_race_editor_id) : QString()},
      {"female", recipe ? recipe->female : true},
      {"vampire", recipe ? recipe->vampire : false},
      {"compatibilityCode", compat_code},
      {"compatibilityLabel", face_compatibility.value("label").toString()},
      {"parametricMode", parametric_mode},
      {"parametricEditable", !morphs_blocked},
      {"parametricSummary", parametric_summary},
      {"geometryKind", parametric_mode},
      {"geometryBadge", morphs_blocked ? QObject::tr("Geometría Importada / Bloqueada")
                                       : (geometry_mode == QLatin1String("preserve") ? QObject::tr("Delta sobre FaceGen")
                                                                                     : QObject::tr("Morfología Paramétrica TRI"))},
      {"geometrySummary", parametric_summary},
      {"allowsParametricMorphs", !morphs_blocked},
      {"allowsPresets", !morphs_blocked},
      {"allowsHeadPartSwap", !head_parts_blocked},
      {"allowsHairColor", !hair_color_blocked},
      {"allowsSkinToneAndTints", !skin_tone_blocked},
      {"presetSlotCount", static_cast<int>(face::kPresetSlotCount)},
      {"tintMaskCount", recipe ? static_cast<int>(recipe->tint_masks.size()) : 0},
      {"morphsAllowed", !morphs_blocked},
      {"morphsBlockedReason", blocked_reason_for(face_compatibility, QStringLiteral("face-morphs"))},
      {"headPartsAllowed", !head_parts_blocked},
      {"headPartsBlockedReason", blocked_reason_for(face_compatibility, QStringLiteral("head-parts"))},
      {"skinToneAllowed", !skin_tone_blocked},
      {"skinToneBlockedReason", blocked_reason_for(face_compatibility, QStringLiteral("skin-tone"))},
      {"hairColorAllowed", !hair_color_blocked},
      {"hairColorBlockedReason", blocked_reason_for(face_compatibility, QStringLiteral("hair-color"))},
      {"regenerateAllowed", !regen_blocked},
      {"regenerateBlockedReason", blocked_reason_for(face_compatibility, QStringLiteral("regenerate"))},
      {"limits", QVariantMap{
                     {"sliderMin", -1.0},
                     {"sliderMax", 1.0},
                     {"customMorphMin", -1.5},
                     {"customMorphMax", 1.5},
                     {"tintAlphaMin", 0.0},
                     {"tintAlphaMax", 1.0},
                     {"weightMin", 0.0},
                     {"weightMax", 100.0},
                     {"heightMin", 0.5},
                     {"heightMax", 2.0}}}};
}

QVariantMap compare_original_vs_proposed(const face::FaceRecipe* base_recipe,
                                         const face::FaceRecipe* current_recipe,
                                         const appearance::FaceState& baseline,
                                         const appearance::FaceState& proposed,
                                         float baseline_height,
                                         float proposed_height,
                                         const QVariantMap& face_compatibility,
                                         const QVariantMap& facegen_report,
                                         bool exportable,
                                         const Mo2Snapshot* snapshot,
                                         const assets::AssetResolver* resolver) {
  QVariantList head_part_changes;
  QVariantList morph_changes;
  QVariantList preset_changes;
  QVariantList tint_changes;
  QVariantList scalar_changes;
  QVariantList provenance_rows;
  QVariantList incompatible_alerts;

  const auto* active_recipe = current_recipe ? current_recipe : base_recipe;
  if (!active_recipe) {
    return {{"loaded", false},
            {"hasChanges", false},
            {"changeCount", 0},
            {"proposalStatus", QStringLiteral("SIN_CAMBIOS")},
            {"proposalStatusLabel", QObject::tr("Sin NPC cargado")}};
  }

  // 1. HeadParts comparison (by slot type)
  auto find_part_by_type = [](const face::FaceRecipe* r, face::HeadPartType t) -> const face::HeadPartData* {
    if (!r) return nullptr;
    for (const auto& p : r->parts) {
      if (!p.extra && p.type == t) return &p;
    }
    return nullptr;
  };

  static const face::HeadPartType kTrackedTypes[] = {
      face::HeadPartType::Face,     face::HeadPartType::Hair,       face::HeadPartType::Eyes,
      face::HeadPartType::Eyebrows, face::HeadPartType::FacialHair, face::HeadPartType::Scars};

  bool any_missing_head_part_asset = false;
  for (const auto t : kTrackedTypes) {
    const auto* orig_p = find_part_by_type(base_recipe, t);
    const auto* prop_p = find_part_by_type(active_recipe, t);
    const std::string orig_fk = orig_p ? orig_p->form_key : std::string();
    const std::string prop_fk = prop_p ? prop_p->form_key : std::string();
    const QString type_code = qs(std::string(face::head_part_type_to_string(t)));
    const QString type_label = head_part_type_es(type_code);

    if (prop_p) {
      auto asset_info = locate_asset_summary(snapshot, resolver, prop_p->model);
      const QString plugin_mod = plugin_provider_of(snapshot, prop_p->plugin);
      provenance_rows.push_back(QVariantMap{
          {"category", type_label},
          {"type", type_code},
          {"formKey", qs(prop_p->form_key)},
          {"editorId", qs(prop_p->editor_id)},
          {"plugin", qs(prop_p->plugin)},
          {"pluginMod", plugin_mod},
          {"assetPath", qs(prop_p->model)},
          {"assetKind", asset_info.value("kind")},
          {"effectiveProvider", asset_info.value("provider").toString().isEmpty() ? plugin_mod : asset_info.value("provider").toString()},
          {"overrideCount", asset_info.value("overrideCount")},
          {"overriddenBy", asset_info.value("overriddenBy")},
          {"found", asset_info.value("found")},
          {"isBald", t == face::HeadPartType::Hair && prop_p->model.empty()}});
      if ((snapshot || resolver) && !prop_p->model.empty() && !asset_info.value("found").toBool()) {
        any_missing_head_part_asset = true;
        incompatible_alerts.push_back(QVariantMap{
            {"severity", "error"},
            {"code", "MissingHeadPartMesh"},
            {"title", QObject::tr("Malla ausente en %1").arg(type_label)},
            {"message", QObject::tr("La pieza «%1» (%2) referencia «%3», que no existe en el perfil MO2.")
                            .arg(qs(prop_p->editor_id), qs(prop_p->plugin), qs(prop_p->model))}});
      }
      if (!prop_p->valid_for_race) {
        incompatible_alerts.push_back(QVariantMap{
            {"severity", "warning"},
            {"code", "HeadPartRaceMismatch"},
            {"title", QObject::tr("Raza incompatible en %1").arg(type_label)},
            {"message", QObject::tr("«%1» no declara compatibilidad con la raza %2.")
                            .arg(qs(prop_p->editor_id), qs(active_recipe->race_editor_id))}});
      }
      if (prop_p->valid_sex != "any" && (prop_p->valid_sex == "female") != active_recipe->female) {
        incompatible_alerts.push_back(QVariantMap{
            {"severity", "warning"},
            {"code", "HeadPartSexMismatch"},
            {"title", QObject::tr("Sexo incompatible en %1").arg(type_label)},
            {"message", QObject::tr("«%1» está diseñada para el sexo opuesto.").arg(qs(prop_p->editor_id))}});
      }
    }

    const bool proposed_has_virtual_bald =
        t == face::HeadPartType::Hair &&
        std::any_of(proposed.head_parts.begin(), proposed.head_parts.end(),
                    [](const std::string& fk) { return fk == kVirtualBaldFormKey; });
    const bool orig_bald = t == face::HeadPartType::Hair && (!orig_p || orig_p->model.empty() || orig_fk == kVirtualBaldFormKey);
    const bool now_bald =
        t == face::HeadPartType::Hair && (!prop_p || prop_p->model.empty() || prop_fk == kVirtualBaldFormKey || proposed_has_virtual_bald);
    if (orig_fk != prop_fk || orig_bald != now_bald) {
      head_part_changes.push_back(QVariantMap{
          {"type", type_code},
          {"typeLabel", type_label},
          {"changeKind", now_bald ? QStringLiteral("bald") : QStringLiteral("replaced")},
          {"originalFormKey", qs(orig_fk)},
          {"originalEditorId", orig_p ? qs(orig_p->editor_id) : QObject::tr("(Ninguno)")},
          {"originalPlugin", orig_p ? qs(orig_p->plugin) : QString()},
          {"originalProvider", orig_p ? plugin_provider_of(snapshot, orig_p->plugin) : QString()},
          {"proposedFormKey", qs(prop_fk)},
          {"proposedEditorId", now_bald ? QObject::tr("Calvo intencional (sin malla de cabello)")
                                        : (prop_p ? qs(prop_p->editor_id) : QObject::tr("(Por defecto)"))},
          {"proposedPlugin", prop_p ? qs(prop_p->plugin) : QString()},
          {"proposedProvider", prop_p ? plugin_provider_of(snapshot, prop_p->plugin) : QString()},
          {"requiresFaceGenRebuild", true}});
    }
  }

  // FaceGen & FaceTint provenance rows
  {
    auto fg_loc = locate_asset_summary(snapshot, resolver, active_recipe->facegen_mesh_path);
    fg_loc["category"] = QObject::tr("FaceGen NIF");
    fg_loc["type"] = QStringLiteral("FaceGenGeometry");
    fg_loc["editorId"] = QFileInfo(qs(active_recipe->facegen_mesh_path).replace('\\', '/')).fileName();
    fg_loc["plugin"] = qs(active_recipe->source_plugin);
    fg_loc["pluginMod"] = plugin_provider_of(snapshot, active_recipe->source_plugin);
    fg_loc["assetPath"] = qs(active_recipe->facegen_mesh_path);
    fg_loc["assetKind"] = fg_loc.value("kind");
    fg_loc["effectiveProvider"] = fg_loc.value("provider");
    provenance_rows.prepend(fg_loc);

    auto ft_loc = locate_asset_summary(snapshot, resolver, active_recipe->facegen_tint_path);
    ft_loc["category"] = QObject::tr("FaceTint DDS");
    ft_loc["type"] = QStringLiteral("FaceGenTint");
    ft_loc["editorId"] = QFileInfo(qs(active_recipe->facegen_tint_path).replace('\\', '/')).fileName();
    ft_loc["plugin"] = qs(active_recipe->source_plugin);
    ft_loc["pluginMod"] = plugin_provider_of(snapshot, active_recipe->source_plugin);
    ft_loc["assetPath"] = qs(active_recipe->facegen_tint_path);
    ft_loc["assetKind"] = ft_loc.value("kind");
    ft_loc["effectiveProvider"] = ft_loc.value("provider");
    provenance_rows.insert(1, ft_loc);
  }

  // 2. Standard facial morphs (NAM9)
  for (const auto& def : face::get_standard_face_morphs()) {
    const float orig_v = baseline.values.sliders[def.index];
    const float prop_v = proposed.values.sliders[def.index];
    if (std::abs(prop_v - orig_v) > 1e-4f) {
      const auto cat = face::morph_category_name(def.category);
      morph_changes.push_back(QVariantMap{
          {"key", qs(def.key)},
          {"label", qs(def.display_name_es)},
          {"category", QString::fromUtf8(cat.data(), static_cast<qsizetype>(cat.size()))},
          {"originalValue", orig_v},
          {"proposedValue", prop_v},
          {"delta", prop_v - orig_v}});
    }
  }
  for (const auto& [name, prop_v] : proposed.values.custom_morphs) {
    const auto it = baseline.values.custom_morphs.find(name);
    const float orig_v = it != baseline.values.custom_morphs.end() ? it->second : 0.0f;
    if (std::abs(prop_v - orig_v) > 1e-4f) {
      morph_changes.push_back(QVariantMap{
          {"key", qs(name)},
          {"label", QObject::tr("RaceMenu: %1").arg(qs(name))},
          {"category", QStringLiteral("Custom")},
          {"originalValue", orig_v},
          {"proposedValue", prop_v},
          {"delta", prop_v - orig_v}});
    }
  }

  // 3. Racial presets (NAMA)
  for (const auto& slot : face::face_preset_slots()) {
    if (slot.prefix.empty()) continue;
    const int orig_p = baseline.values.presets[slot.index];
    const int prop_p = proposed.values.presets[slot.index];
    if (orig_p != prop_p) {
      preset_changes.push_back(QVariantMap{
          {"slot", static_cast<int>(slot.index)},
          {"key", qs(slot.key)},
          {"label", qs(slot.display_name_es)},
          {"originalValue", orig_p},
          {"proposedValue", prop_p}});
    }
  }

  // 4. Tint layers
  for (const auto& prop_t : proposed.values.tints) {
    const auto it = std::find_if(baseline.values.tints.begin(), baseline.values.tints.end(),
                                 [&](const auto& bt) { return bt.index == prop_t.index; });
    const face::Rgb8 orig_c = it != baseline.values.tints.end() ? it->color : face::Rgb8{255, 255, 255};
    const float orig_val = it != baseline.values.tints.end() ? it->value : 0.0f;
    if (!(orig_c == prop_t.color) || std::abs(orig_val - prop_t.value) > 1e-3f) {
      const auto* mask = active_recipe->tint_mask(prop_t.index);
      tint_changes.push_back(QVariantMap{
          {"index", static_cast<int>(prop_t.index)},
          {"maskType", mask ? mask->mask_type : -1},
          {"label", mask ? QFileInfo(qs(mask->mask_path).replace('\\', '/')).completeBaseName()
                         : QObject::tr("Capa #%1").arg(prop_t.index)},
          {"originalColor", rgb_to_qcolor(orig_c).name()},
          {"proposedColor", rgb_to_qcolor(prop_t.color).name()},
          {"originalAlpha", orig_val},
          {"proposedAlpha", prop_t.value}});
    }
  }

  // 5. Scalars (HairColor, SkinTone, Weight, Height, HeadTexture, GeometryMode)
  if (baseline.values.hair_color != proposed.values.hair_color) {
    scalar_changes.push_back(QVariantMap{
        {"key", "hairColor"},
        {"label", QObject::tr("Color de cabello (HCLF / HairTint)")},
        {"originalValue", baseline.values.hair_color ? rgb_to_qcolor(*baseline.values.hair_color).name() : QObject::tr("(Por defecto)")},
        {"proposedValue", proposed.values.hair_color ? rgb_to_qcolor(*proposed.values.hair_color).name() : QObject::tr("(Por defecto)")}});
  }
  if (!(baseline.values.skin_tone == proposed.values.skin_tone)) {
    scalar_changes.push_back(QVariantMap{
        {"key", "skinTone"},
        {"label", QObject::tr("Tono de piel (QNAM)")},
        {"originalValue", rgb_to_qcolor(baseline.values.skin_tone).name()},
        {"proposedValue", rgb_to_qcolor(proposed.values.skin_tone).name()}});
  }
  if (std::abs(baseline.values.weight - proposed.values.weight) > 1e-3f) {
    scalar_changes.push_back(QVariantMap{
        {"key", "weight"},
        {"label", QObject::tr("Peso corporal/facial (NAM7)")},
        {"originalValue", QString::number(baseline.values.weight, 'f', 0)},
        {"proposedValue", QString::number(proposed.values.weight, 'f', 0)}});
  }
  if (std::abs(baseline_height - proposed_height) > 1e-4f) {
    scalar_changes.push_back(QVariantMap{
        {"key", "height"},
        {"label", QObject::tr("Altura (NAM6)")},
        {"originalValue", QString::number(baseline_height, 'f', 2)},
        {"proposedValue", QString::number(proposed_height, 'f', 2)}});
  }
  if (baseline.head_texture != proposed.head_texture) {
    scalar_changes.push_back(QVariantMap{
        {"key", "headTexture"},
        {"label", QObject::tr("Textura facial base (FTST)")},
        {"originalValue", baseline.head_texture.empty() ? QObject::tr("(Raza por defecto)") : qs(baseline.head_texture)},
        {"proposedValue", proposed.head_texture.empty() ? QObject::tr("(Raza por defecto)") : qs(proposed.head_texture)}});
  }

  for (const auto& b : face_compatibility.value("blocked").toList()) {
    const auto bm = b.toMap();
    incompatible_alerts.push_back(QVariantMap{
        {"severity", "warning"},
        {"code", "BlockedOperation_" + bm.value("op").toString()},
        {"title", QObject::tr("Bloqueo de protección: %1").arg(bm.value("label").toString())},
        {"message", bm.value("reason").toString()}});
  }
  if (const auto conflict = face_compatibility.value("provenance").toMap().value("conflict").toString(); !conflict.isEmpty()) {
    incompatible_alerts.push_back(QVariantMap{
        {"severity", "warning"},
        {"code", "ProvenanceMismatch"},
        {"title", QObject::tr("Desajuste entre plugin ganador y FaceGen en MO2")},
        {"message", conflict}});
  }

  const int change_count = static_cast<int>(head_part_changes.size() + morph_changes.size() + preset_changes.size() +
                                            tint_changes.size() + scalar_changes.size());
  const bool has_changes = change_count > 0;
  const auto build_kind = appearance::face_build_kind(baseline, proposed);
  const bool virtual_bald = std::find(proposed.head_parts.begin(), proposed.head_parts.end(), std::string(kVirtualBaldFormKey)) !=
                            proposed.head_parts.end();

  QString build_mode = QStringLiteral("unchanged");
  if (has_changes) {
    if (build_kind == appearance::FaceBuildKind::HairColorSafe) {
      build_mode = QStringLiteral("hair-color-safe");
    } else if (proposed.geometry_mode == "preserve") {
      build_mode = QStringLiteral("preserve-delta");
    } else {
      build_mode = QStringLiteral("full-regenerate");
    }
  }

  QString proposal_status = QStringLiteral("SIN_CAMBIOS");
  QString proposal_label = QObject::tr("Sin modificaciones pendientes");
  QString proposal_detail = QObject::tr("La apariencia actual coincide exactamente con el registro ganador del perfil.");

  if (has_changes) {
    if (any_missing_head_part_asset) {
      proposal_status = QStringLiteral("BLOQUEADA_RECURSOS_FALTANTES");
      proposal_label = QObject::tr("Propuesta bloqueada: faltan recursos en MO2");
      proposal_detail = QObject::tr("Una o más piezas seleccionadas tienen mallas o texturas ausentes. No se generará un mod roto.");
    } else if (virtual_bald || !exportable) {
      proposal_status = QStringLiteral("PENDIENTE_RECONSTRUCCION");
      proposal_label = QObject::tr("Propuesta marcada como PENDIENTE (sin exportación directa segura)");
      proposal_detail = virtual_bald
                            ? QObject::tr("La vista previa muestra al NPC calvo intencionalmente, pero el orden de carga no incluye un registro HeadPart vacío compatible para escribir en PNAM sin generar referencias inválidas. Puedes guardar la propuesta en la biblioteca como pendiente.")
                            : QObject::tr("El cambio requiere una reconstrucción facial que aún está en curso o bloqueada por la geometría personalizada del NPC. Se guarda como propuesta pendiente sin generar un mod aparentemente funcional.");
    } else {
      proposal_status = QStringLiteral("LISTA_PARA_EXPORTAR");
      proposal_label = build_mode == QLatin1String("hair-color-safe")
                           ? QObject::tr("Lista para exportar (Modo seguro de color de cabello)")
                           : QObject::tr("Lista para exportar (Reconstrucción FaceGen verificada)");
      proposal_detail = build_mode == QLatin1String("hair-color-safe")
                            ? QObject::tr("Solo cambia HCLF / HairTint sobre el FaceGen original byte a byte. No requiere reconstruir la geometría facial.")
                            : QObject::tr("El FaceGen NIF y el FaceTint DDS han sido reconstruidos en memoria y coinciden con la vista previa. Pendiente: verificación dentro de Skyrim (PENDIENTE_SKYRIM).");
    }
  }

  return {
      {"loaded", true},
      {"hasChanges", has_changes},
      {"changeCount", change_count},
      {"buildMode", build_mode},
      {"requiresFaceGenRebuild", !head_part_changes.isEmpty() || !morph_changes.isEmpty() || !preset_changes.isEmpty()},
      {"proposalStatus", proposal_status},
      {"proposalStatusLabel", proposal_label},
      {"proposalStatusDetail", proposal_detail},
      {"headPartChanges", head_part_changes},
      {"morphChanges", morph_changes},
      {"presetChanges", preset_changes},
      {"tintChanges", tint_changes},
      {"scalarChanges", scalar_changes},
      {"provenanceSummary", provenance_rows},
      {"incompatibleAlerts", incompatible_alerts},
      {"faceGenStatus", facegen_report.value("status").toString()}};
}

QVariantList next_gen_style_presets() {
  return {
      QVariantMap{
          {"id", QStringLiteral("vanilla-remastered")},
          {"name", QStringLiteral("Vanilla Remastered")},
          {"tagline", QObject::tr("Identidad original conservada · Detalles visuales mejorados · Compatibilidad máxima")},
          {"description", QObject::tr("Conserva el 100% de la morfología facial original (NAM9/NAMA) y las piezas de cabeza del NPC, armonizando la iluminación de piel (QNAM) con la capa base de FaceTint y suavizando tintes extremos sin alterar su identidad ni romper cabezas personalizadas.")},
          {"badge", QStringLiteral("Máxima Compatibilidad")},
          {"rules", QStringList{
                        QObject::tr("Conserva intactos los sliders NAM9 y presets raciales NAMA del registro."),
                        QObject::tr("Sincroniza la capa de tinte de piel con TextureLighting (QNAM)."),
                        QObject::tr("Normaliza opacidades excesivas de maquillaje/pintura (>65%) para evitar artefactos de compresión BC3."),
                        QObject::tr("Mantiene el modo Preserve / HairColorSafe en cabezas esculpidas.")}}},
      QVariantMap{
          {"id", QStringLiteral("lore-friendly-hd")},
          {"name", QStringLiteral("Lore Friendly HD")},
          {"tagline", QObject::tr("Texturas y peinados de calidad · Coherencia con Skyrim · Respeto de raza, edad y personalidad")},
          {"description", QObject::tr("Selecciona opciones compatibles de alta calidad instaladas en el perfil MO2 que respeten la raza, sexo y condición (vampirismo/edad) del NPC, aplicando un acabado natural de piel y rasgos sin desvirtuar la estética de Skyrim.")},
          {"badge", QStringLiteral("Coherente con Lore")},
          {"rules", QStringList{
                        QObject::tr("Respeta estrictamente las restricciones de raza, sexo y condición vampírica."),
                        QObject::tr("Si hay peinados u ojos mejorados compatibles en MO2 y la cabeza no está bloqueada, sugiere la variante compatible verificada."),
                        QObject::tr("Suaviza asimetrías extremas manteniendo el 85% de la firma morfológica original."),
                        QObject::tr("Aplica definición natural en labios, pómulos y cuenca ocular dentro del rango [0.18, 0.38].")}}},
      QVariantMap{
          {"id", QStringLiteral("cinematic-fantasy")},
          {"name", QStringLiteral("Cinematic Fantasy")},
          {"tagline", QObject::tr("Apariencias visualmente elaboradas · Cabello y rasgos sofisticados · Control manual sobre el resultado")},
          {"description", QObject::tr("Aplica reglas estéticas avanzadas: prioriza peinados con física HDT-SMP o paquetes detallados (KS Hairdos, Apachii, HG Hair si están instalados en el perfil), proporciones faciales estilizadas dentro de los límites técnicos y maquillaje multicapa.")},
          {"badge", QStringLiteral("Estilizado / HD")},
          {"rules", QStringList{
                        QObject::tr("Prioriza peinados detallados o con física SMP instalados en MO2 cuya malla NIF esté verificada."),
                        QObject::tr("Refina puente nasal, línea mandibular, mirada y labios dentro del margen técnico seguro [-0.65, +0.65]."),
                        QObject::tr("Aplica capas de delineado, sombra ocular y tinte labial armonizadas con el tono de piel."),
                        QObject::tr("Permite activar o desactivar manualmente cada grupo de reglas (Cabello, Ojos, Morfología, Maquillaje).")}}}};
}

StyleApplicationResult apply_next_gen_style(const QString& style_id,
                                             const QVariantMap& options,
                                             const face::FaceRecipe& recipe,
                                             const appearance::FaceState& baseline,
                                             const appearance::FaceState& current,
                                             const QVariantList& head_part_options,
                                             const QVariantMap& face_compatibility) {
  StyleApplicationResult res;
  res.style_id = style_id;
  res.next_state = current;

  const bool apply_hair = options.value("applyHair", true).toBool();
  const bool apply_eyes = options.value("applyEyes", true).toBool();
  const bool apply_morphs = options.value("applyMorphs", true).toBool();
  const bool apply_tints = options.value("applyTints", true).toBool();
  const float intensity = std::clamp(options.value("intensity", 1.0).toFloat(), 0.1f, 1.0f);

  const bool morphs_blocked = is_op_blocked(face_compatibility, QStringLiteral("face-morphs"));
  const bool parts_blocked = is_op_blocked(face_compatibility, QStringLiteral("head-parts"));
  const bool tints_blocked = is_op_blocked(face_compatibility, QStringLiteral("skin-tone"));

  auto set_tint_layer = [&](int mask_type, const face::Rgb8& color, float alpha) {
    for (const auto& mask : recipe.tint_masks) {
      if (mask.mask_type != mask_type) continue;
      auto it = std::find_if(res.next_state.values.tints.begin(), res.next_state.values.tints.end(),
                             [&](const auto& t) { return t.index == mask.index; });
      if (it == res.next_state.values.tints.end()) {
        res.next_state.values.tints.push_back({mask.index, color, std::clamp(alpha, 0.0f, 1.0f), -1});
      } else {
        it->color = color;
        it->value = std::clamp(alpha, 0.0f, 1.0f);
      }
      return true;
    }
    return false;
  };

  auto replace_head_part_slot = [&](face::HeadPartType slot_type, const std::string& new_form_key) {
    std::erase(res.next_state.head_parts, std::string(kVirtualBaldFormKey));
    std::erase_if(res.next_state.head_parts, [&](const std::string& fk) {
      const auto it = std::find_if(recipe.parts.begin(), recipe.parts.end(), [&](const auto& p) { return p.form_key == fk; });
      return it != recipe.parts.end() && it->type == slot_type;
    });
    res.next_state.head_parts.push_back(new_form_key);
  };

  if (style_id == QLatin1String("vanilla-remastered")) {
    res.style_name = QStringLiteral("Vanilla Remastered");
    if (!morphs_blocked && apply_morphs) {
      res.next_state.values.sliders = baseline.values.sliders;
      res.next_state.values.presets = baseline.values.presets;
      res.applied_rules << QObject::tr("Morfología original del registro (NAM9/NAMA) restaurada íntegramente.");
    } else if (morphs_blocked) {
      res.skipped_rules << QObject::tr("Morfología omitida: cabeza personalizada protegida.");
    }

    if (!tints_blocked && apply_tints) {
      for (auto& t : res.next_state.values.tints) {
        const auto* mask = recipe.tint_mask(t.index);
        if (mask && mask->mask_type == face::kTintMaskSkinTone) {
          t.color = res.next_state.values.skin_tone;
          t.value = 1.0f;
        } else if (t.value > 0.65f) {
          t.value = 0.65f;
        }
      }
      set_tint_layer(face::kTintMaskSkinTone, res.next_state.values.skin_tone, 1.0f);
      res.applied_rules << QObject::tr("Tono de piel (QNAM) sincronizado con FaceTint y opacidades extremas suavizadas.");
    } else if (tints_blocked) {
      res.skipped_rules << QObject::tr("Tintes omitidos: tono de piel bloqueado por cabeza personalizada.");
    }
    res.next_state.source = "Estilo: Vanilla Remastered";
  } else if (style_id == QLatin1String("lore-friendly-hd")) {
    res.style_name = QStringLiteral("Lore Friendly HD");
    if (!morphs_blocked && apply_morphs) {
      for (std::size_t i = 0; i < res.next_state.values.sliders.size(); ++i) {
        const float orig = baseline.values.sliders[i];
        res.next_state.values.sliders[i] = std::clamp(orig * (1.0f - 0.15f * intensity), -1.0f, 1.0f);
      }
      if (const auto* cheek = face::find_morph_def("CheeksUpVsDown")) {
        res.next_state.values.sliders[cheek->index] = std::clamp(baseline.values.sliders[cheek->index] + 0.12f * intensity, -1.0f, 1.0f);
      }
      res.applied_rules << QObject::tr("Proporciones faciales suavizadas conservando el 85% de la identidad racial original.");
    } else if (morphs_blocked) {
      res.skipped_rules << QObject::tr("Morfología omitida: cabeza personalizada protegida.");
    }

    if (!parts_blocked && apply_hair) {
      QString chosen_hair_fk, chosen_hair_name;
      for (const auto& v : head_part_options) {
        const auto m = v.toMap();
        if (m.value("type").toString() != QLatin1String("Hair")) continue;
        if (!m.value("meshFound", true).toBool() || m.value("isBaldPart", false).toBool()) continue;
        chosen_hair_fk = m.value("formKey").toString();
        chosen_hair_name = m.value("name").toString();
        if (m.value("plugin").toString().compare(QLatin1String("Skyrim.esm"), Qt::CaseInsensitive) != 0) break;
      }
      if (!chosen_hair_fk.isEmpty()) {
        replace_head_part_slot(face::HeadPartType::Hair, ss(chosen_hair_fk));
        res.applied_rules << QObject::tr("Peinado compatible seleccionado: %1 (%2).").arg(chosen_hair_name, chosen_hair_fk);
      } else {
        res.skipped_rules << QObject::tr("Se conserva el cabello original (no se hallaron alternativas adicionales cargadas en la galería).");
      }
    } else if (parts_blocked && apply_hair) {
      res.skipped_rules << QObject::tr("Cambio de cabello omitido: piezas de cabeza bloqueadas en este FaceGen.");
    }

    if (!tints_blocked && apply_tints) {
      set_tint_layer(face::kTintMaskSkinTone, res.next_state.values.skin_tone, 1.0f);
      if (set_tint_layer(3, face::Rgb8{148, 78, 76}, 0.25f * intensity)) {
        res.applied_rules << QObject::tr("Tinte labial natural aplicado (opacidad %1%).").arg(std::lround(25 * intensity));
      }
      if (set_tint_layer(1, face::Rgb8{165, 95, 88}, 0.20f * intensity)) {
        res.applied_rules << QObject::tr("Rubor facial sutil aplicado en pómulos.");
      }
    } else if (tints_blocked) {
      res.skipped_rules << QObject::tr("Tintes omitidos: tono de piel bloqueado.");
    }
    res.next_state.source = "Estilo: Lore Friendly HD";
  } else if (style_id == QLatin1String("cinematic-fantasy")) {
    res.style_name = QStringLiteral("Cinematic Fantasy");
    if (!morphs_blocked && apply_morphs) {
      struct Rule { const char* key; float target; };
      static const Rule kCinematicMorphs[] = {
          {"NoseLongVsShort", 0.15f},  {"NoseUpVsDown", -0.18f},   {"JawNarrowVsWide", -0.22f},
          {"CheeksUpVsDown", 0.28f},   {"CheeksForwardVsBack", 0.15f}, {"EyesUpVsDown", 0.12f},
          {"LipsUpVsDown", 0.15f},     {"ChinNarrowVsWide", -0.18f}};
      for (const auto& r : kCinematicMorphs) {
        if (const auto* def = face::find_morph_def(r.key)) {
          const float base_v = baseline.values.sliders[def->index];
          res.next_state.values.sliders[def->index] = def->clamp(base_v + (r.target - base_v) * (0.65f * intensity));
        }
      }
      res.applied_rules << QObject::tr("Esculpido paramétrico Cinematic Fantasy aplicado en nariz, pómulos, mandíbula y labios.");
    } else if (morphs_blocked && apply_morphs) {
      res.skipped_rules << QObject::tr("Esculpido paramétrico omitido: la cabeza personalizada bloquea morphs.");
    }

    if (!parts_blocked && (apply_hair || apply_eyes)) {
      if (apply_hair) {
        QString best_hair_fk, best_hair_name;
        for (const auto& v : head_part_options) {
          const auto m = v.toMap();
          if (m.value("type").toString() != QLatin1String("Hair")) continue;
          if (!m.value("meshFound", true).toBool() || m.value("isBaldPart", false).toBool()) continue;
          best_hair_fk = m.value("formKey").toString();
          best_hair_name = m.value("name").toString();
          if (m.value("hasSmp", false).toBool() ||
              m.value("packageFamily").toString() != QLatin1String("Vanilla / Creation Club")) {
            break;
          }
        }
        if (!best_hair_fk.isEmpty()) {
          replace_head_part_slot(face::HeadPartType::Hair, ss(best_hair_fk));
          res.applied_rules << QObject::tr("Peinado estilizado seleccionado: %1.").arg(best_hair_name);
        }
      }
      if (apply_eyes) {
        for (const auto& v : head_part_options) {
          const auto m = v.toMap();
          if (m.value("type").toString() != QLatin1String("Eyes") || !m.value("meshFound", true).toBool()) continue;
          replace_head_part_slot(face::HeadPartType::Eyes, ss(m.value("formKey").toString()));
          res.applied_rules << QObject::tr("Variante ocular seleccionada: %1.").arg(m.value("name").toString());
          break;
        }
      }
    } else if (parts_blocked && (apply_hair || apply_eyes)) {
      res.skipped_rules << QObject::tr("Reemplazo de piezas de cabeza omitido por bloqueo de compatibilidad.");
    }

    if (!tints_blocked && apply_tints) {
      set_tint_layer(face::kTintMaskSkinTone, res.next_state.values.skin_tone, 1.0f);
      set_tint_layer(2, face::Rgb8{24, 20, 28}, 0.52f * intensity);     // Eyeliner
      set_tint_layer(4, face::Rgb8{58, 38, 64}, 0.36f * intensity);     // Upper eye socket
      set_tint_layer(3, face::Rgb8{162, 54, 64}, 0.45f * intensity);    // Lips
      set_tint_layer(1, face::Rgb8{178, 88, 84}, 0.28f * intensity);    // Cheeks
      res.applied_rules << QObject::tr("Maquillaje multicapa Cinematic Fantasy aplicado sobre las máscaras válidas de la raza.");
    } else if (tints_blocked && apply_tints) {
      res.skipped_rules << QObject::tr("Maquillaje omitido: capas de tinte bloqueadas.");
    }
    res.next_state.source = "Estilo: Cinematic Fantasy";
  }

  res.changed = !(res.next_state == current);
  res.summary = res.applied_rules.isEmpty()
                    ? QObject::tr("No se pudo aplicar ninguna regla del estilo «%1» debido a las restricciones de compatibilidad del NPC.")
                          .arg(res.style_name)
                    : QObject::tr("Estilo «%1» aplicado (%2 regla(s) activas, %3 omitidas).")
                          .arg(res.style_name)
                          .arg(res.applied_rules.size())
                          .arg(res.skipped_rules.size());
  return res;
}

QVariantMap run_visual_quality_check(const face::FaceRecipe* recipe,
                                     const appearance::FaceState& baseline,
                                     const appearance::FaceState& current,
                                     bool intentionally_bald,
                                     const QVariantMap& face_compatibility,
                                     const QVariantMap& facegen_report,
                                     const QVariantList& scene_parts,
                                     const QVariantMap& scene_stats,
                                     const QVariantList& scene_issues,
                                     const QVariantMap& skin_match,
                                     const Mo2Snapshot* snapshot,
                                     const assets::AssetResolver* resolver) {
  Q_UNUSED(baseline);
  QVariantList checks;
  int error_count = 0;
  int warning_count = 0;

  auto add_check = [&](const QString& key, const QString& title, const QString& status, const QString& code,
                       const QString& summary, const QVariantMap& metrics = {}) {
    if (status == QLatin1String("error")) ++error_count;
    else if (status == QLatin1String("warning")) ++warning_count;
    checks.push_back(QVariantMap{
        {"id", key},
        {"key", key},
        {"title", title},
        {"status", status},
        {"code", code},
        {"summary", summary},
        {"metrics", metrics}});
  };

  auto parse_bounds = [](const QVariantMap& row, QVector3D* out_min, QVector3D* out_max) -> bool {
    if (row.contains("min") && row.contains("max")) {
      const auto min_l = row.value("min").toList();
      const auto max_l = row.value("max").toList();
      if (min_l.size() >= 3 && max_l.size() >= 3) {
        *out_min = QVector3D(min_l[0].toFloat(), min_l[1].toFloat(), min_l[2].toFloat());
        *out_max = QVector3D(max_l[0].toFloat(), max_l[1].toFloat(), max_l[2].toFloat());
        return true;
      }
    }
    const QString bounds_str = row.value("inspector").toMap().value("bounds").toString();
    // Format: "(x1, y1, z1) - (x2, y2, z2)"
    static const QRegularExpression re(
        QStringLiteral("\\(([-0-9.]+),\\s*([-0-9.]+),\\s*([-0-9.]+)\\)\\s*-\\s*\\(([-0-9.]+),\\s*([-0-9.]+),\\s*([-0-9.]+)\\)"));
    const auto m = re.match(bounds_str);
    if (m.hasMatch()) {
      *out_min = QVector3D(m.captured(1).toFloat(), m.captured(2).toFloat(), m.captured(3).toFloat());
      *out_max = QVector3D(m.captured(4).toFloat(), m.captured(5).toFloat(), m.captured(6).toFloat());
      return true;
    }
    if (row.contains("center") && row.contains("radius")) {
      const QVector3D c = row.value("center").value<QVector3D>();
      const float r = row.value("radius").toFloat();
      *out_min = c - QVector3D(r, r, r);
      *out_max = c + QVector3D(r, r, r);
      return true;
    }
    return false;
  };

  // Gather head, hair, facial, and body parts from the 3D scene
  bool has_head_bounds = false;
  QVector3D head_min, head_max;
  bool has_body_bounds = false;
  QVector3D body_min, body_max;
  int visible_hair_shapes = 0;
  int hidden_hair_shapes = 0;
  int total_hair_vertices = 0;
  QStringList displaced_elements;

  for (const auto& pv : scene_parts) {
    const auto row = pv.toMap();
    const QString group = row.value("group").toString();
    const QString shape = row.value("shape", row.value("shapeName")).toString();
    const QString path = row.value("path", row.value("meshPath")).toString().toLower();
    const bool shown = row.value("shownByDefault", row.value("visible", true)).toBool();
    const bool is_hair = row.value("hair").toBool() || path.contains("hair") || shape.contains("hair", Qt::CaseInsensitive);

    if (is_hair && !shape.contains("brow", Qt::CaseInsensitive) && !shape.contains("beard", Qt::CaseInsensitive)) {
      if (shown) {
        ++visible_hair_shapes;
        total_hair_vertices += row.value("inspector").toMap().value("vertices", 100).toInt();
      } else {
        ++hidden_hair_shapes;
      }
    }

    QVector3D bmin, bmax;
    if (!parse_bounds(row, &bmin, &bmax)) continue;

    if (group == QLatin1String("head") && !is_hair) {
      const bool is_primary_face = shape.contains("head", Qt::CaseInsensitive) ||
                                   shape.contains("face", Qt::CaseInsensitive) || !has_head_bounds;
      if (is_primary_face && (!has_head_bounds || (bmax - bmin).length() > (head_max - head_min).length())) {
        head_min = bmin;
        head_max = bmax;
        has_head_bounds = true;
      }
    } else if (group == QLatin1String("skin")) {
      const QString slots_str = row.value("inspector").toMap().value("slots").toString();
      if (slots_str.contains("32") || !has_body_bounds) {
        body_min = bmin;
        body_max = bmax;
        has_body_bounds = true;
      }
    }
  }

  // 1. Check: Cabeza y cuello desalineados (Neck alignment + CIE76 skin match)
  {
    const QString skin_verdict = skin_match.value("verdict").toString();
    const double delta_e = skin_match.value("worst", 0.0).toDouble();
    float v_gap = 0.0f;
    float horiz_offset = 0.0f;
    bool geom_misaligned = false;
    if (has_head_bounds && has_body_bounds) {
      const bool y_up = (body_max.y() - body_min.y()) >= (body_max.z() - body_min.z());
      const QVector3D hc = (head_min + head_max) * 0.5f;
      const QVector3D bc = (body_min + body_max) * 0.5f;
      if (y_up) {
        v_gap = head_min.y() - body_max.y();
        horiz_offset = std::hypot(hc.x() - bc.x(), hc.z() - bc.z());
      } else {
        v_gap = head_min.z() - body_max.z();
        horiz_offset = std::hypot(hc.x() - bc.x(), hc.y() - bc.y());
      }
      if (v_gap > 2.5f || v_gap < -18.0f || horiz_offset > 5.0f) {
        geom_misaligned = true;
      }
    }
    const QVariantMap metrics{{"neckGap", v_gap}, {"neckZGap", v_gap}, {"neckXYOffset", horiz_offset}, {"skinDeltaE", delta_e}, {"skinVerdict", skin_verdict}};
    if (geom_misaligned) {
      add_check(QStringLiteral("neckAlignment"), QObject::tr("Alineación de cabeza y cuello"), QStringLiteral("warning"),
                QStringLiteral("NeckGapDetected"),
                QObject::tr("Desalineación geométrica detectada entre cabeza y torso (brecha vertical = %1 u, desplazamiento = %2 u).")
                    .arg(v_gap, 0, 'f', 2)
                    .arg(horiz_offset, 0, 'f', 2),
                metrics);
    } else if (skin_verdict == QLatin1String("mismatch") || skin_verdict == QLatin1String("noticeable")) {
      add_check(QStringLiteral("neckAlignment"), QObject::tr("Alineación de cabeza y cuello"), QStringLiteral("warning"),
                QStringLiteral("NeckColorSeam"),
                QObject::tr("Geometría de cuello alineada, pero se detecta diferencia cromática cabeza-cuerpo (ΔE = %1).")
                    .arg(delta_e, 0, 'f', 1),
                metrics);
    } else {
      add_check(QStringLiteral("neckAlignment"), QObject::tr("Alineación de cabeza y cuello"), QStringLiteral("ok"),
                QStringLiteral("NeckAligned"),
                QObject::tr("Unión cabeza-cuello dentro de tolerancia geométrica y cromática (ΔE = %1).").arg(delta_e, 0, 'f', 1),
                metrics);
    }
  }

  // 2. Check: Cabello aparentemente ausente vs Calvo intencional
  {
    bool expects_hair = !intentionally_bald;
    QString expected_hair_id;
    if (recipe && expects_hair) {
      bool found_non_empty_hair = false;
      for (const auto& p : recipe->parts) {
        if (p.type == face::HeadPartType::Hair) {
          expected_hair_id = qs(p.editor_id);
          if (!p.model.empty()) found_non_empty_hair = true;
        }
      }
      expects_hair = found_non_empty_hair;
    }

    bool hair_build_failed = false;
    for (const auto& pv : facegen_report.value("parts").toList()) {
      const auto pm = pv.toMap();
      if (pm.value("editorId").toString() == expected_hair_id &&
          pm.value("status").toString() != QLatin1String("built") &&
          pm.value("status").toString() != QLatin1String("no-model")) {
        hair_build_failed = true;
      }
    }

    const QVariantMap metrics{{"intentionallyBald", !expects_hair},
                              {"visibleHairShapes", visible_hair_shapes},
                              {"hiddenHairShapes", hidden_hair_shapes},
                              {"hairVertices", total_hair_vertices}};

    if (!expects_hair) {
      add_check(QStringLiteral("hairPresence"), QObject::tr("Presencia e integridad del cabello"), QStringLiteral("ok"),
                QStringLiteral("IntentionallyBaldVerified"),
                QObject::tr("NPC configurado intencionalmente sin cabello (calvo): no se espera geometría de cabello en el visor."),
                metrics);
    } else if (hair_build_failed || (!scene_parts.isEmpty() && visible_hair_shapes == 0 && hidden_hair_shapes == 0)) {
      add_check(QStringLiteral("hairPresence"), QObject::tr("Presencia e integridad del cabello"), QStringLiteral("error"),
                QStringLiteral("MissingHairGeometry"),
                QObject::tr("Cabello aparentemente ausente: el registro asigna «%1», pero no hay geometría de cabello visible en la escena 3D.")
                    .arg(expected_hair_id),
                metrics);
    } else if (visible_hair_shapes == 0 && hidden_hair_shapes > 0) {
      add_check(QStringLiteral("hairPresence"), QObject::tr("Presencia e integridad del cabello"), QStringLiteral("ok"),
                QStringLiteral("HairCoveredByEquipment"),
                QObject::tr("El cabello (%1 forma(s)) está oculto correctamente bajo un casco o capucha equipada.").arg(hidden_hair_shapes),
                metrics);
    } else {
      add_check(QStringLiteral("hairPresence"), QObject::tr("Presencia e integridad del cabello"), QStringLiteral("ok"),
                QStringLiteral("HairPresent"),
                QObject::tr("Geometría de cabello representada correctamente (%1 forma(s), %2 vértices).")
                    .arg(visible_hair_shapes)
                    .arg(total_hair_vertices),
                metrics);
    }
  }

  // 3. Check: Elementos faciales desplazados
  {
    if (has_head_bounds) {
      const QVector3D margin(10.0f, 10.0f, 14.0f);
      const QVector3D env_min = head_min - margin;
      const QVector3D env_max = head_max + margin;
      for (const auto& pv : scene_parts) {
        const auto row = pv.toMap();
        if (row.value("group").toString() != QLatin1String("head")) continue;
        if (row.value("hair").toBool()) continue;
        QVector3D bmin, bmax;
        if (!parse_bounds(row, &bmin, &bmax)) continue;
        const QVector3D c = (bmin + bmax) * 0.5f;
        if (c.x() < env_min.x() || c.x() > env_max.x() ||
            c.y() < env_min.y() || c.y() > env_max.y() ||
            c.z() < env_min.z() || c.z() > env_max.z()) {
          displaced_elements << row.value("shape").toString();
        }
      }
    }
    if (!displaced_elements.isEmpty()) {
      add_check(QStringLiteral("facialElementAlignment"), QObject::tr("Posición de elementos faciales"), QStringLiteral("error"),
                QStringLiteral("DisplacedFacialElements"),
                QObject::tr("Elementos faciales desplazados fuera del volumen de la cabeza: %1.")
                    .arg(displaced_elements.join(QStringLiteral(", "))),
                {{"displacedShapes", displaced_elements}});
    } else {
      add_check(QStringLiteral("facialElementAlignment"), QObject::tr("Posición de elementos faciales"), QStringLiteral("ok"),
                QStringLiteral("FacialElementsAligned"),
                QObject::tr("Ojos, cejas, boca y piezas faciales contenidos dentro del volumen anatómico de la cabeza."));
    }
  }

  // 4. Check: Texturas que no pueden resolverse
  {
    QStringList missing_tex;
    const int stats_missing = scene_stats.value("missingTextures", 0).toInt();
    for (const auto& iv : scene_issues) {
      const auto im = iv.toMap();
      const QString code = im.value("code").toString();
      if (code.contains("Texture", Qt::CaseInsensitive) || code.contains("Tint", Qt::CaseInsensitive)) {
        const QString subj = im.value("subject").toString();
        if (!subj.isEmpty() && !missing_tex.contains(subj)) missing_tex << subj;
      }
    }
    if (recipe && resolver) {
      for (const auto& p : recipe->parts) {
        if (!p.texture_set) continue;
        for (const auto& t : *p.texture_set) {
          if (!t.empty() && !resolver->locate(t).has_value()) {
            const QString tq = qs(t);
            if (!missing_tex.contains(tq, Qt::CaseInsensitive)) missing_tex << tq;
          }
        }
      }
    }
    if (stats_missing > 0 || !missing_tex.isEmpty()) {
      add_check(QStringLiteral("textureResolution"), QObject::tr("Resolución de texturas DDS"), QStringLiteral("error"),
                QStringLiteral("UnresolvedTextures"),
                QObject::tr("Se detectaron %1 textura(s) sin resolver en el perfil MO2: %2")
                    .arg(std::max(stats_missing, static_cast<int>(missing_tex.size())))
                    .arg(missing_tex.isEmpty() ? QObject::tr("(ver diagnósticos de escena)") : missing_tex.join(QStringLiteral(", "))),
                {{"missingCount", std::max(stats_missing, static_cast<int>(missing_tex.size()))}, {"missingPaths", missing_tex}});
    } else {
      add_check(QStringLiteral("textureResolution"), QObject::tr("Resolución de texturas DDS"), QStringLiteral("ok"),
                QStringLiteral("AllTexturesResolved"),
                QObject::tr("Todas las texturas faciales, oculares, capilares y máscaras de tinte se resolvieron en MO2."));
    }
  }

  // 5. Check: Partes de geometría no representadas
  {
    QStringList unrepresented;
    for (const auto& pv : facegen_report.value("parts").toList()) {
      const auto pm = pv.toMap();
      const QString st = pm.value("status").toString();
      if (!st.isEmpty() && st != QLatin1String("built") && st != QLatin1String("no-model")) {
        unrepresented << (pm.value("editorId").toString() + QStringLiteral(" (") + st + QStringLiteral(")"));
      }
    }
    if (recipe && resolver) {
      for (const auto& p : recipe->parts) {
        if (p.model.empty()) continue;
        if (!resolver->locate(p.model).has_value()) {
          const QString item = qs(p.editor_id) + QStringLiteral(" (missing-mesh)");
          if (!unrepresented.contains(item)) unrepresented << item;
        }
      }
    }
    if (!unrepresented.isEmpty()) {
      add_check(QStringLiteral("geometryRepresentation"), QObject::tr("Representación de partes de geometría"), QStringLiteral("error"),
                QStringLiteral("UnrepresentedGeometryParts"),
                QObject::tr("Partes de geometría facial no representadas en el FaceGen/visor: %1.")
                    .arg(unrepresented.join(QStringLiteral(", "))),
                {{"unrepresentedParts", unrepresented}});
    } else {
      add_check(QStringLiteral("geometryRepresentation"), QObject::tr("Representación de partes de geometría"), QStringLiteral("ok"),
                QStringLiteral("AllGeometryRepresented"),
                QObject::tr("Todas las piezas de cabeza activas con malla NIF están representadas en el modelo construido."));
    }
  }

  // 6. Check: Aspecto diferente al preset esperado
  {
    QStringList divergences;
    if (!current.source.empty()) {
      for (const auto& b : face_compatibility.value("blocked").toList()) {
        divergences << b.toMap().value("label").toString();
      }
    }
    for (const auto& pv : facegen_report.value("parts").toList()) {
      const QStringList missing_morphs = pv.toMap().value("missing").toStringList();
      if (!missing_morphs.isEmpty()) {
        divergences << QObject::tr("%1: morphs ausentes (%2)")
                           .arg(pv.toMap().value("editorId").toString(), missing_morphs.join(QStringLiteral(", ")));
      }
    }
    if (const auto conflict = face_compatibility.value("provenance").toMap().value("conflict").toString(); !conflict.isEmpty()) {
      divergences << conflict;
    }
    if (!divergences.isEmpty()) {
      add_check(QStringLiteral("presetFidelity"), QObject::tr("Fidelidad respecto al preset / registro esperado"), QStringLiteral("warning"),
                QStringLiteral("PresetDivergence"),
                QObject::tr("El aspecto puede diferir del preset o registro esperado: %1").arg(divergences.join(QStringLiteral("; "))),
                {{"divergences", divergences}});
    } else {
      add_check(QStringLiteral("presetFidelity"), QObject::tr("Fidelidad respecto al preset / registro esperado"), QStringLiteral("ok"),
                QStringLiteral("PresetFidelityVerified"),
                QObject::tr("Los morphs, piezas y tintes activos se aplicaron sin bloqueos ni discrepancias de topología."));
    }
  }

  const QString overall = error_count > 0 ? QStringLiteral("error") : (warning_count > 0 ? QStringLiteral("warning") : QStringLiteral("ok"));
  const QStringList renderer_limitations{
      QObject::tr("El visor 3D utiliza Qt Quick 3D con una aproximación de BSLightingShaderProperty (FaceTint, SkinTint, HairTint, EyeEnvmap) en pose de referencia."),
      QObject::tr("No ejecuta el motor Creation Engine de Skyrim SE/AE ni shaders de ENB / Community Shaders ni dispersión subsuperficial (SSS) en tiempo de juego."),
      QObject::tr("Los huesos HDT-SMP se muestran en pose de referencia estática sin simulación física en vivo."),
      QObject::tr("Estado máximo sin prueba humana en el juego: PENDIENTE_SKYRIM (inGameVerified: false).")};

  return {
      {"overallStatus", overall},
      {"errorCount", error_count},
      {"warningCount", warning_count},
      {"passedCount", static_cast<int>(checks.size()) - error_count - warning_count},
      {"totalChecks", static_cast<int>(checks.size())},
      {"checks", checks},
      {"rendererLimitations", renderer_limitations},
      {"checkedAt", now_iso()},
      {"npcFormKey", recipe ? qs(recipe->npc_form_key) : QString()},
      {"profileName", snapshot ? qs(snapshot->profile.name) : QString()},
      {"inGameVerified", false},
      {"verificationStatus", QStringLiteral("PENDIENTE_SKYRIM")},
      {"inGameStatus", QStringLiteral("PENDIENTE_SKYRIM")}};
}

// ------------------------------------------------------------------------------------- BeautyLibraryStore

BeautyLibraryStore::BeautyLibraryStore(QString directory)
    : dir_(directory.trimmed().isEmpty() ? default_directory() : std::move(directory)) {
  load();
}

QString BeautyLibraryStore::default_directory() {
  if (qEnvironmentVariableIsSet("SNS_BEAUTY_LIBRARY_DIR")) {
    return qEnvironmentVariable("SNS_BEAUTY_LIBRARY_DIR");
  }
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/beauty_library");
}

QString BeautyLibraryStore::file_path() const { return QDir(dir_).filePath(QStringLiteral("library.json")); }
QString BeautyLibraryStore::previews_directory() const { return QDir(dir_).filePath(QStringLiteral("previews")); }

bool BeautyLibraryStore::load(QString* error) {
  QDir().mkpath(dir_);
  QDir().mkpath(previews_directory());
  QFile f(file_path());
  if (!f.exists()) {
    presets_.clear();
    favorite_hairs_.clear();
    head_eye_combos_.clear();
    facial_configs_.clear();
    imported_resources_.clear();
    return true;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  QJsonParseError parse_err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &parse_err);
  if (parse_err.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) *error = parse_err.errorString();
    return false;
  }
  const auto root = doc.object();
  presets_ = root.value("presets").toArray().toVariantList();
  favorite_hairs_ = root.value("favoriteHairs").toArray().toVariantList();
  head_eye_combos_ = root.value("headEyeCombos").toArray().toVariantList();
  facial_configs_ = root.value("facialConfigs").toArray().toVariantList();
  imported_resources_ = root.value("importedResources").toArray().toVariantList();
  return true;
}

bool BeautyLibraryStore::save(QString* error) const {
  QDir().mkpath(dir_);
  QDir().mkpath(previews_directory());
  QSaveFile f(file_path());
  if (!f.open(QIODevice::WriteOnly)) {
    if (error) *error = f.errorString();
    return false;
  }
  QJsonObject root{
      {"schemaVersion", 1},
      {"updatedAt", now_iso()},
      {"presets", QJsonArray::fromVariantList(presets_)},
      {"favoriteHairs", QJsonArray::fromVariantList(favorite_hairs_)},
      {"headEyeCombos", QJsonArray::fromVariantList(head_eye_combos_)},
      {"facialConfigs", QJsonArray::fromVariantList(facial_configs_)},
      {"importedResources", QJsonArray::fromVariantList(imported_resources_)}};
  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (f.write(bytes) != bytes.size() || !f.commit()) {
    if (error) *error = f.errorString();
    return false;
  }
  return true;
}

QStringList BeautyLibraryStore::favorite_hair_keys() const {
  QStringList keys;
  for (const auto& v : favorite_hairs_) {
    const QString fk = v.toMap().value("formKey").toString();
    if (!fk.isEmpty()) keys << fk;
  }
  return keys;
}

QString BeautyLibraryStore::save_preview_image(const QString& id, const QImage& image) const {
  if (image.isNull()) return {};
  QDir().mkpath(previews_directory());
  QString safe_id = id;
  safe_id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
  const QString out_path = QDir(previews_directory()).filePath(safe_id + QStringLiteral(".png"));
  if (image.save(out_path, "PNG")) return out_path;
  return {};
}

namespace {

QVariantMap serialize_face_state(const appearance::FaceState& state, float height, bool intentionally_bald) {
  QVariantList sliders;
  for (const float s : state.values.sliders) sliders << s;
  QVariantList presets;
  for (const int p : state.values.presets) presets << p;
  QVariantList tints;
  for (const auto& t : state.values.tints) {
    tints.push_back(QVariantMap{
        {"index", static_cast<int>(t.index)},
        {"color", rgb_to_qcolor(t.color).name()},
        {"value", t.value},
        {"preset", t.preset}});
  }
  QStringList head_parts;
  for (const auto& hp : state.head_parts) head_parts << qs(hp);
  QVariantMap custom_morphs;
  for (const auto& [k, v] : state.values.custom_morphs) custom_morphs[qs(k)] = v;

  return {
      {"sliders", sliders},
      {"presets", presets},
      {"weight", state.values.weight},
      {"height", height},
      {"skinTone", rgb_to_qcolor(state.values.skin_tone).name()},
      {"hairColor", state.values.hair_color ? rgb_to_qcolor(*state.values.hair_color).name() : QString()},
      {"tints", tints},
      {"headParts", head_parts},
      {"headTexture", qs(state.head_texture)},
      {"geometryMode", qs(state.geometry_mode)},
      {"intentionallyBald", intentionally_bald},
      {"customMorphs", custom_morphs}};
}

QStringList collect_required_plugins(const face::FaceRecipe& recipe, const QVariantList& associated_resources) {
  QStringList plugins;
  for (const auto& p : recipe.parts) {
    const QString pl = qs(p.plugin);
    if (!pl.isEmpty() && !plugins.contains(pl, Qt::CaseInsensitive)) plugins << pl;
  }
  for (const auto& rv : associated_resources) {
    const QString pl = rv.toMap().value("plugin").toString();
    if (!pl.isEmpty() && !plugins.contains(pl, Qt::CaseInsensitive)) plugins << pl;
  }
  return plugins;
}

}  // namespace

QString BeautyLibraryStore::save_preset(const QString& name,
                                        const QString& category,
                                        const QString& notes,
                                        const face::FaceRecipe& recipe,
                                        const appearance::FaceState& state,
                                        float height,
                                        bool intentionally_bald,
                                        const QString& export_state,
                                        const QString& export_state_reason,
                                        const QVariantList& associated_resources,
                                        const QString& preview_image_path,
                                        QString* error) {
  const QString id = QStringLiteral("preset-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
  const QStringList req_plugins = collect_required_plugins(recipe, associated_resources);
  QVariantMap entry{
      {"id", id},
      {"kind", QStringLiteral("preset")},
      {"name", name.trimmed().isEmpty() ? QObject::tr("Preset sin título") : name.trimmed()},
      {"category", category.trimmed().isEmpty() ? QStringLiteral("Custom") : category.trimmed()},
      {"notes", notes.trimmed()},
      {"npcFormKey", qs(recipe.npc_form_key)},
      {"raceEditorId", qs(recipe.race_editor_id)},
      {"morphRace", qs(recipe.morph_race_editor_id)},
      {"female", recipe.female},
      {"vampire", recipe.vampire},
      {"exportState", export_state.isEmpty() ? QStringLiteral("READY_FOR_EXPORT") : export_state},
      {"exportStateReason", export_state_reason},
      {"inGameStatus", QStringLiteral("PENDIENTE_SKYRIM")},
      {"faceState", serialize_face_state(state, height, intentionally_bald)},
      {"associatedResources", associated_resources},
      {"compatibility", QVariantMap{
                            {"raceEditorId", qs(recipe.race_editor_id)},
                            {"morphRace", qs(recipe.morph_race_editor_id)},
                            {"female", recipe.female},
                            {"requiresRegenerate", state.geometry_mode == "regenerate"},
                            {"requiredPlugins", req_plugins}}},
      {"previewImagePath", preview_image_path},
      {"previewImage", preview_image_path},
      {"createdAt", now_iso()}};
  presets_.prepend(entry);
  if (!save(error)) return {};
  return id;
}

bool BeautyLibraryStore::delete_preset(const QString& id, QString* error) {
  const auto before = presets_.size();
  presets_.erase(std::remove_if(presets_.begin(), presets_.end(),
                                [&](const QVariant& v) { return v.toMap().value("id").toString() == id; }),
                 presets_.end());
  if (presets_.size() == before) return false;
  return save(error);
}

QVariantMap BeautyLibraryStore::find_preset(const QString& id) const {
  for (const auto& v : presets_) {
    const auto m = v.toMap();
    if (m.value("id").toString() == id) return m;
  }
  return {};
}

bool BeautyLibraryStore::toggle_favorite_hair(const QVariantMap& hair_info, QString* error) {
  const QString fk = hair_info.value("formKey").toString();
  if (fk.isEmpty()) return false;
  for (auto it = favorite_hairs_.begin(); it != favorite_hairs_.end(); ++it) {
    if (it->toMap().value("formKey").toString().compare(fk, Qt::CaseInsensitive) == 0) {
      favorite_hairs_.erase(it);
      return save(error);
    }
  }
  QVariantMap entry{
      {"formKey", fk},
      {"editorId", hair_info.value("editorId").toString()},
      {"name", hair_info.value("name").toString().isEmpty() ? hair_info.value("editorId").toString() : hair_info.value("name").toString()},
      {"plugin", hair_info.value("plugin").toString()},
      {"pluginMod", hair_info.value("pluginMod").toString()},
      {"effectiveProvider", hair_info.value("effectiveProvider").toString()},
      {"packageFamily", hair_info.value("packageFamily").toString()},
      {"modelPath", hair_info.value("modelPath").toString()},
      {"hasSmp", hair_info.value("hasSmp", false).toBool()},
      {"isBaldPart", hair_info.value("isBaldPart", false).toBool()},
      {"previewImagePath", hair_info.value("previewImagePath").toString()},
      {"addedAt", now_iso()}};
  favorite_hairs_.prepend(entry);
  return save(error);
}

bool BeautyLibraryStore::is_favorite_hair(const QString& form_key) const {
  for (const auto& v : favorite_hairs_) {
    if (v.toMap().value("formKey").toString().compare(form_key, Qt::CaseInsensitive) == 0) return true;
  }
  return false;
}

QString BeautyLibraryStore::save_head_eye_combo(const QString& name,
                                                const face::FaceRecipe& recipe,
                                                const appearance::FaceState& state,
                                                const QVariantList& associated_resources,
                                                const QString& preview_image_path,
                                                QString* error) {
  const QString id = QStringLiteral("combo-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
  QStringList head_parts;
  for (const auto& hp : state.head_parts) head_parts << qs(hp);
  const QStringList req_plugins = collect_required_plugins(recipe, associated_resources);

  QVariantMap entry{
      {"id", id},
      {"kind", QStringLiteral("head-eye-combo")},
      {"name", name.trimmed().isEmpty() ? QObject::tr("Combinación Cabeza + Ojos") : name.trimmed()},
      {"raceEditorId", qs(recipe.race_editor_id)},
      {"female", recipe.female},
      {"headParts", head_parts},
      {"headTexture", qs(state.head_texture)},
      {"hairColor", state.values.hair_color ? rgb_to_qcolor(*state.values.hair_color).name() : QString()},
      {"skinTone", rgb_to_qcolor(state.values.skin_tone).name()},
      {"associatedResources", associated_resources},
      {"compatibility", QVariantMap{
                            {"raceEditorId", qs(recipe.race_editor_id)},
                            {"morphRace", qs(recipe.morph_race_editor_id)},
                            {"female", recipe.female},
                            {"requiresRegenerate", true},
                            {"requiredPlugins", req_plugins}}},
      {"previewImagePath", preview_image_path},
      {"previewImage", preview_image_path},
      {"createdAt", now_iso()}};
  head_eye_combos_.prepend(entry);
  if (!save(error)) return {};
  return id;
}

bool BeautyLibraryStore::delete_head_eye_combo(const QString& id, QString* error) {
  const auto before = head_eye_combos_.size();
  head_eye_combos_.erase(std::remove_if(head_eye_combos_.begin(), head_eye_combos_.end(),
                                        [&](const QVariant& v) { return v.toMap().value("id").toString() == id; }),
                         head_eye_combos_.end());
  if (head_eye_combos_.size() == before) return false;
  return save(error);
}

QString BeautyLibraryStore::save_facial_config(const QString& name,
                                               const face::FaceRecipe& recipe,
                                               const appearance::FaceState& state,
                                               const QString& preview_image_path,
                                               QString* error) {
  const QString id = QStringLiteral("facecfg-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
  QVariantMap entry{
      {"id", id},
      {"kind", QStringLiteral("facial-config")},
      {"name", name.trimmed().isEmpty() ? QObject::tr("Configuración Facial") : name.trimmed()},
      {"raceEditorId", qs(recipe.race_editor_id)},
      {"female", recipe.female},
      {"faceState", serialize_face_state(state, recipe.height, false)},
      {"compatibility", QVariantMap{
                            {"raceEditorId", qs(recipe.race_editor_id)},
                            {"morphRace", qs(recipe.morph_race_editor_id)},
                            {"female", recipe.female},
                            {"requiresRegenerate", false},
                            {"requiredPlugins", QStringList{}}}},
      {"previewImagePath", preview_image_path},
      {"previewImage", preview_image_path},
      {"createdAt", now_iso()}};
  facial_configs_.prepend(entry);
  if (!save(error)) return {};
  return id;
}

bool BeautyLibraryStore::delete_facial_config(const QString& id, QString* error) {
  const auto before = facial_configs_.size();
  facial_configs_.erase(std::remove_if(facial_configs_.begin(), facial_configs_.end(),
                                       [&](const QVariant& v) { return v.toMap().value("id").toString() == id; }),
                        facial_configs_.end());
  if (facial_configs_.size() == before) return false;
  return save(error);
}

QVariantMap BeautyLibraryStore::import_compatible_resource(const QString& file_path,
                                                           const QString& category_tag,
                                                           QString* error) {
  const QFileInfo info(file_path);
  if (!info.exists() || !info.isFile()) {
    if (error) *error = QObject::tr("El archivo a importar no existe: %1").arg(file_path);
    return {};
  }
  const QString ext = info.suffix().toLower();
  const QString id = QStringLiteral("res-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
  QVariantMap record{
      {"id", id},
      {"name", info.completeBaseName()},
      {"sourcePath", info.absoluteFilePath()},
      {"format", ext},
      {"categoryTag", category_tag.trimmed().isEmpty() ? QStringLiteral("User Import") : category_tag.trimmed()},
      {"redistributesProtectedAssets", false},
      {"importedAt", now_iso()}};

  if (ext == QLatin1String("jslot")) {
    try {
      const auto preset = presets::parse_jslot(file_io::read_text_file(std::filesystem::path(info.absoluteFilePath().toStdWString())),
                                               ss(info.completeBaseName()));
      QStringList plugins, head_parts;
      for (const auto& pl : preset.plugins) plugins << qs(pl);
      for (const auto& hp : preset.head_parts) head_parts << qs(hp.form_key);
      record["formatVersion"] = preset.format_version;
      record["requiredPlugins"] = plugins;
      record["headParts"] = head_parts;
      record["morphCount"] = static_cast<int>((preset.has_sliders ? face::kSliderCount : 0) + preset.custom_morphs.size());
      record["tintCount"] = static_cast<int>(preset.tints.size());
      record["sculptHosts"] = static_cast<int>(preset.sculpt.size());
      record["compatibility"] = QVariantMap{
          {"requiresRegenerate", true},
          {"requiredPlugins", plugins}};
    } catch (const std::exception& e) {
      if (error) *error = QObject::tr("El preset .jslot no es válido: %1").arg(qs(e.what()));
      return {};
    }
  } else if (ext == QLatin1String("json")) {
    QFile f(info.absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
      if (error) *error = f.errorString();
      return {};
    }
    QJsonParseError perr{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
      if (error) *error = QObject::tr("El archivo JSON no es válido: %1").arg(perr.errorString());
      return {};
    }
    const auto obj = doc.object().toVariantMap();
    record["name"] = obj.value("name", info.completeBaseName()).toString();
    record["compatibility"] = obj.value("compatibility").toMap();
    record["faceState"] = obj.value("faceState").toMap();
    if (obj.contains("faceState")) {
      presets_.prepend(obj);
    }
  } else {
    if (error) *error = QObject::tr("Formato no soportado para importación («.%1»). Usa .jslot o .json.").arg(ext);
    return {};
  }

  imported_resources_.prepend(record);
  if (!save(error)) return {};
  return record;
}

bool BeautyLibraryStore::delete_imported_resource(const QString& id, QString* error) {
  const auto before = imported_resources_.size();
  imported_resources_.erase(std::remove_if(imported_resources_.begin(), imported_resources_.end(),
                                           [&](const QVariant& v) { return v.toMap().value("id").toString() == id; }),
                            imported_resources_.end());
  if (imported_resources_.size() == before) return false;
  return save(error);
}

QVariantMap BeautyLibraryStore::evaluate_entry_compatibility(const QVariantMap& entry,
                                                             const face::FaceRecipe* target_recipe,
                                                             const QVariantMap& face_compatibility,
                                                             const Mo2Snapshot* snapshot) const {
  const auto compat = entry.value("compatibility").toMap();
  QStringList reasons;
  QStringList warnings;
  bool blocking = false;

  const QStringList req_plugins = compat.value("requiredPlugins").toStringList();
  QStringList missing_plugins;
  if (snapshot) {
    for (const auto& pl : req_plugins) {
      if (!plugin_is_active(snapshot, ss(pl))) missing_plugins << pl;
    }
  }
  if (!missing_plugins.isEmpty()) {
    blocking = true;
    reasons << QObject::tr("Faltan plugins requeridos en el perfil: %1").arg(missing_plugins.join(QStringLiteral(", ")));
  }

  if (target_recipe) {
    if (compat.contains("female") && compat.value("female").toBool() != target_recipe->female) {
      blocking = true;
      reasons << QObject::tr("Incompatibilidad de sexo: la entrada es para %1 y el NPC es %2.")
                     .arg(compat.value("female").toBool() ? QObject::tr("Mujer") : QObject::tr("Hombre"),
                          target_recipe->female ? QObject::tr("Mujer") : QObject::tr("Hombre"));
    }
    const QString entry_race = compat.value("raceEditorId").toString();
    if (!entry_race.isEmpty() &&
        entry_race.compare(qs(target_recipe->race_editor_id), Qt::CaseInsensitive) != 0 &&
        entry_race.compare(qs(target_recipe->morph_race_editor_id), Qt::CaseInsensitive) != 0) {
      warnings << QObject::tr("Raza distinta (%1 vs %2): las máscaras de tinte específicas se adaptarán a la raza actual.")
                      .arg(entry_race, qs(target_recipe->race_editor_id));
    }
    const QString kind = entry.value("kind").toString();
    if ((kind == QLatin1String("preset") || kind == QLatin1String("facial-config")) &&
        is_op_blocked(face_compatibility, QStringLiteral("face-morphs"))) {
      warnings << QObject::tr("El NPC tiene geometría personalizada que bloquea morphs: solo se aplicarán las propiedades permitidas.");
    }
    if ((kind == QLatin1String("preset") || kind == QLatin1String("head-eye-combo")) &&
        is_op_blocked(face_compatibility, QStringLiteral("head-parts"))) {
      if (kind == QLatin1String("head-eye-combo")) blocking = true;
      reasons << QObject::tr("Reemplazo de piezas de cabeza bloqueado para este NPC.");
    }
  }

  const QString state = blocking                 ? QStringLiteral("incompatible")
                        : !warnings.isEmpty()    ? QStringLiteral("warning")
                                                 : QStringLiteral("compatible");
  return {
      {"compatible", !blocking},
      {"state", state},
      {"missingPlugins", missing_plugins},
      {"reasons", reasons},
      {"warnings", warnings}};
}

std::optional<appearance::FaceState> BeautyLibraryStore::apply_entry_to_state(const QVariantMap& entry,
                                                                              const face::FaceRecipe& target_recipe,
                                                                              const appearance::FaceState& current,
                                                                              const QVariantMap& face_compatibility,
                                                                              QStringList* warnings) const {
  const auto check = evaluate_entry_compatibility(entry, &target_recipe, face_compatibility, nullptr);
  if (!check.value("compatible").toBool()) {
    if (warnings) *warnings = check.value("reasons").toStringList();
    return std::nullopt;
  }
  if (warnings) *warnings = check.value("warnings").toStringList();

  const bool morphs_blocked = is_op_blocked(face_compatibility, QStringLiteral("face-morphs"));
  const bool parts_blocked = is_op_blocked(face_compatibility, QStringLiteral("head-parts"));
  const bool skin_blocked = is_op_blocked(face_compatibility, QStringLiteral("skin-tone"));
  const bool hair_blocked = is_op_blocked(face_compatibility, QStringLiteral("hair-color"));

  appearance::FaceState next = current;
  const QString kind = entry.value("kind").toString();

  if (kind == QLatin1String("head-eye-combo")) {
    if (parts_blocked) return std::nullopt;
    next.head_parts.clear();
    for (const auto& hp : entry.value("headParts").toStringList()) next.head_parts.push_back(ss(hp));
    next.head_texture = ss(entry.value("headTexture").toString());
    if (!hair_blocked) {
      const QColor hc(entry.value("hairColor").toString());
      if (hc.isValid()) next.values.hair_color = qcolor_to_rgb(hc);
    }
    if (!skin_blocked) {
      const QColor sc(entry.value("skinTone").toString());
      if (sc.isValid()) next.values.skin_tone = qcolor_to_rgb(sc);
    }
    next.source = "Biblioteca: " + ss(entry.value("name").toString());
    return next;
  }

  const QVariantMap fs = entry.value("faceState").toMap();
  if (fs.isEmpty()) return std::nullopt;

  if (!morphs_blocked) {
    const auto sliders = fs.value("sliders").toList();
    for (std::size_t i = 0; i < next.values.sliders.size() && i < static_cast<std::size_t>(sliders.size()); ++i) {
      next.values.sliders[i] = std::clamp(sliders[static_cast<qsizetype>(i)].toFloat(), -1.0f, 1.0f);
    }
    const auto presets = fs.value("presets").toList();
    for (std::size_t i = 0; i < next.values.presets.size() && i < static_cast<std::size_t>(presets.size()); ++i) {
      next.values.presets[i] = std::clamp(presets[static_cast<qsizetype>(i)].toInt(), -1, 99);
    }
    if (fs.contains("weight")) {
      next.values.weight = std::clamp(fs.value("weight").toFloat(), 0.0f, 100.0f);
    }
    next.values.custom_morphs.clear();
    const auto cm = fs.value("customMorphs").toMap();
    for (auto it = cm.begin(); it != cm.end(); ++it) {
      next.values.custom_morphs[ss(it.key())] = std::clamp(it.value().toFloat(), -1.5f, 1.5f);
    }
  }

  if (!skin_blocked) {
    const QColor sc(fs.value("skinTone").toString());
    if (sc.isValid()) next.values.skin_tone = qcolor_to_rgb(sc);
    const QString entry_race = entry.value("raceEditorId").toString();
    if (entry_race.isEmpty() || entry_race.compare(qs(target_recipe.race_editor_id), Qt::CaseInsensitive) == 0) {
      next.values.tints.clear();
      for (const auto& tv : fs.value("tints").toList()) {
        const auto tm = tv.toMap();
        const int idx = tm.value("index").toInt();
        if (!target_recipe.tint_mask(static_cast<std::uint16_t>(idx))) continue;
        const QColor tc(tm.value("color").toString());
        next.values.tints.push_back({static_cast<std::uint16_t>(idx),
                                     tc.isValid() ? qcolor_to_rgb(tc) : face::Rgb8{255, 255, 255},
                                     std::clamp(tm.value("value").toFloat(), 0.0f, 1.0f),
                                     tm.value("preset", -1).toInt()});
      }
    }
  }

  if (kind == QLatin1String("preset")) {
    if (!hair_blocked) {
      const QString hc_str = fs.value("hairColor").toString();
      if (!hc_str.isEmpty()) {
        const QColor hc(hc_str);
        if (hc.isValid()) next.values.hair_color = qcolor_to_rgb(hc);
      }
    }
    if (!parts_blocked) {
      next.head_parts.clear();
      for (const auto& hp : fs.value("headParts").toStringList()) next.head_parts.push_back(ss(hp));
      if (fs.value("intentionallyBald").toBool() &&
          std::find(next.head_parts.begin(), next.head_parts.end(), std::string(kVirtualBaldFormKey)) == next.head_parts.end()) {
        next.head_parts.push_back(kVirtualBaldFormKey);
      }
      next.head_texture = ss(fs.value("headTexture").toString());
    }
  }

  next.source = "Biblioteca: " + ss(entry.value("name").toString());
  return next;
}

}  // namespace sns::app
