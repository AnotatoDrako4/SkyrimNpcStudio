#include "npc_health_audit.hpp"

#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

#include "json_bridge.hpp"
#include "resource_context.hpp"
#include "sns/core/text.hpp"
#include "sns/face/facegen.hpp"
#include "sns/nif/mesh.hpp"

namespace sns::app {
namespace {

QStringList to_qstrings(const std::vector<std::string>& items) {
  QStringList out;
  for (const auto& s : items) out << qs(s);
  return out;
}

std::string plugin_mod(const SnapshotPtr& snapshot, const std::string& plugin) {
  const auto it = snapshot->plugin_provider.find(text::to_lower_ascii(plugin));
  return it == snapshot->plugin_provider.end() ? std::string() : it->second;
}

// Effective copy of a Data-relative file (loose over archives, like the game) and every loose copy in MO2.
// Same rule as the control map's FaceGen sources (studio_controller build_control_inputs).
appearance::AssetSource asset_source(const SnapshotPtr& snapshot, const ResourceContext* ctx, const std::string& path) {
  appearance::AssetSource a;
  a.path = path;
  if (path.empty()) return a;
  for (const auto i : snapshot->vfs.providers_of(path)) a.loose_mods.push_back(snapshot->vfs.providers()[i].name);
  const auto loc = ctx && ctx->resolver ? ctx->resolver->locate(path) : std::nullopt;
  if (loc) {
    a.found = true;
    a.kind = loc->kind == assets::AssetLocation::Kind::Loose ? "loose" : "bsa";
    a.winner_mod = a.kind == "bsa" && !loc->archive_provider.empty() ? loc->archive_provider : loc->provider;
  } else if (!a.loose_mods.empty()) {
    a.found = true;
    a.kind = "loose";
    a.winner_mod = a.loose_mods.back();
  }
  return a;
}

// HDPT models are stored relative to Data\meshes ("Actors\Character\...\Hair09.nif"); the VFS wants "meshes\...".
std::string mesh_data_path(std::string path) {
  std::replace(path.begin(), path.end(), '/', '\\');
  while (!path.empty() && path.front() == '\\') path.erase(path.begin());
  if (path.empty()) return path;
  if (text::to_lower_ascii(path).rfind("data\\", 0) == 0) path.erase(0, 5);
  if (text::to_lower_ascii(path).rfind("meshes\\", 0) != 0) path = "meshes\\" + path;
  return path;
}

bool contains_ci(const QStringList& list, const char* value) {
  return std::any_of(list.begin(), list.end(), [&](const QString& s) { return s.compare(QLatin1String(value), Qt::CaseInsensitive) == 0; });
}

std::vector<std::string> missing_masters(const SnapshotPtr& snapshot, const std::string& plugin) {
  std::vector<std::string> out;
  const auto& active = snapshot->load_order.active;
  const auto it = std::find_if(active.begin(), active.end(), [&](const mo2::ResolvedPlugin& p) { return text::iequals_ascii(p.name, plugin); });
  if (it == active.end() || !it->header) return out;
  for (const auto& m : it->header->masters) {
    const bool present = std::any_of(active.begin(), active.end(), [&](const mo2::ResolvedPlugin& p) { return text::iequals_ascii(p.name, m); });
    if (!present) out.push_back(m);
  }
  return out;
}

// Bones of the race skeleton ("NPC Head [Head]", "NPC Spine2 [Spn2]"...). Anything else a FaceGen hair is skinned to
// comes from the hair's own NIF: SMP/physics bones.
bool is_skeleton_bone(const std::string& name) { return name.rfind("NPC ", 0) == 0 || name == "NPC"; }

// SMP state of one hair shape of the effective FaceGen; nullopt when the shape is skinned only to skeleton bones (a
// static hair: nothing SMP to check). Compares with the head part's own NIF what the Creation Kit loses when it
// bakes a physics hair: the XML path and the bones' rest pose. May throw (NIF parse); the caller catches.
std::optional<appearance::AuditSmpFaceGenHair> smp_hair_facts(const SnapshotPtr& snapshot, const ResourceContext* ctx, const nif::MeshFile& facegen,
                                                              const nif::Shape& shape, const std::vector<appearance::AuditHeadPart>& parts) {
  appearance::AuditSmpFaceGenHair h;
  h.shape = shape.name;
  for (const auto& b : shape.bones) {
    if (!is_skeleton_bone(b)) h.physics_bones.push_back(b);
  }
  if (h.physics_bones.empty()) return std::nullopt;
  h.facegen_has_physics_xml = !facegen.physics_xml.empty();
  if (!facegen.physics_xml.empty()) h.physics_xml = asset_source(snapshot, ctx, facegen.physics_xml);
  const auto part = std::find_if(parts.begin(), parts.end(), [&](const appearance::AuditHeadPart& p) { return text::iequals_ascii(p.editor_id, shape.name); });
  if (part == parts.end() || part->model_path.empty() || !part->mesh.found || !ctx || !ctx->resolver) return h;  // nothing to compare with
  const auto bytes = ctx->resolver->read(part->model_path);
  if (!bytes) return h;
  const auto source = nif::load_mesh(*bytes, part->model_path);
  h.source_mesh = part->model_path;
  h.source_physics_xml = source.physics_xml;
  if (!source.physics_xml.empty()) h.physics_xml = asset_source(snapshot, ctx, source.physics_xml);
  // Rest pose (NIF root space) of every bone some shape of the hair's NIF is skinned to.
  std::map<std::string, std::optional<nif::Transform>> source_bones;
  for (const auto& sh : source.shapes) {
    for (std::size_t i = 0; i < sh.bones.size(); ++i) {
      auto& slot = source_bones[text::to_lower_ascii(sh.bones[i])];
      if (!slot && i < sh.mesh_bone_to_global.size()) slot = sh.mesh_bone_to_global[i];
    }
  }
  float max_offset = 0.0f;
  bool compared = false;
  for (std::size_t i = 0; i < shape.bones.size(); ++i) {
    if (is_skeleton_bone(shape.bones[i])) continue;
    const auto it = source_bones.find(text::to_lower_ascii(shape.bones[i]));
    if (it == source_bones.end()) {
      h.bones_missing_in_source.push_back(shape.bones[i]);
      continue;
    }
    if (!it->second || i >= shape.mesh_bone_to_global.size() || !shape.mesh_bone_to_global[i]) continue;
    const auto& a = shape.mesh_bone_to_global[i]->translation;
    const auto& b = it->second->translation;
    max_offset = std::max(max_offset, std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)));
    compared = true;
  }
  if (compared) h.max_bone_offset = max_offset;
  return h;
}

// Default-outfit armors in the Hair slot (31) and, from the getAppearance answer the scan attached as
// "hairArmorAppearance", the addons the game draws for this NPC's race and sex.
constexpr unsigned kHairSlotBit = 1u << (31 - 30);

std::vector<appearance::AuditHairArmor> hair_armors(const SnapshotPtr& snapshot, const ResourceContext* ctx, const QVariantMap& details) {
  std::vector<appearance::AuditHairArmor> out;
  const auto appearance = details.value("hairArmorAppearance").toMap();
  const bool resolved = !appearance.isEmpty() && !appearance.contains("error");
  for (const auto& v : details.value("defaultOutfit").toMap().value("armors").toList()) {
    const auto armor = v.toMap();
    if (!(armor.value("slotMask").toUInt() & kHairSlotBit)) continue;
    appearance::AuditHairArmor w;
    w.form_key = ss(armor.value("formKey").toString());
    w.editor_id = ss(armor.value("editorId").toString());
    w.plugin = ss(armor.value("winningPlugin").toString());
    w.record_mod = plugin_mod(snapshot, w.plugin);
    w.smp = armor.value("hasSmp").toBool();
    if (resolved) {
      int addons = 0;
      for (const auto& p : appearance.value("pieces").toList()) {
        const auto piece = p.toMap();
        if (!text::iequals_ascii(ss(piece.value("armorFormKey").toString()), w.form_key)) continue;
        for (const auto& a : piece.value("addons").toList()) {
          ++addons;
          auto mesh = asset_source(snapshot, ctx, mesh_data_path(ss(a.toMap().value("meshPath").toString())));
          if (mesh.found && ctx && ctx->resolver) {
            try {
              if (const auto bytes = ctx->resolver->read(mesh.path)) {
                const auto xml = nif::load_mesh(*bytes, mesh.path).physics_xml;
                if (!xml.empty()) w.physics.push_back(asset_source(snapshot, ctx, xml));
              }
            } catch (const std::exception&) {
              // An unreadable wig mesh still counts as present; its physics is simply unknown.
            }
          }
          w.meshes.push_back(std::move(mesh));
        }
      }
      w.addons_for_race = addons;
    }
    out.push_back(std::move(w));
  }
  return out;
}

// NPCs the owner reported bald in game (SNS_OBSERVED_BALD="FormKey,FormKey"): reported, never inferred.
bool observed_bald(const std::string& form_key) {
  static const QStringList reported = qEnvironmentVariable("SNS_OBSERVED_BALD").split(',', Qt::SkipEmptyParts);
  return std::any_of(reported.begin(), reported.end(), [&](const QString& r) { return text::iequals_ascii(ss(r.trimmed()), form_key); });
}

QVariantMap asset_map(const appearance::AssetSource& a) {
  return {{"path", qs(a.path)}, {"found", a.found}, {"kind", qs(a.kind)}, {"winnerMod", qs(a.winner_mod)}, {"looseMods", to_qstrings(a.loose_mods)}};
}

}  // namespace

NpcHealthEntry audit_npc_health(const SnapshotPtr& snapshot, const QSet<QString>& races_with_facegen, const QVariantMap& details,
                                const appearance::ControlInputs& inputs, const appearance::ControlReport& report) {
  NpcHealthEntry entry;
  auto& s = entry.subject;
  if (!snapshot) {
    entry.audit = appearance::audit_npc_appearance(s, inputs, report);
    return entry;
  }
  const auto summary = details.value("summary").toMap();
  s.form_key = ss(summary.value("formKey").toString());
  s.editor_id = ss(summary.value("editorId").toString());
  s.name = ss(summary.value("name").toString());
  s.defining_plugin = ss(summary.value("originPlugin").toString());
  s.winning_plugin = ss(summary.value("winningPlugin").toString());
  // Same template rule as the control map (expects_facegen): a Traits template supplies head, hair and FaceGen.
  s.uses_traits_template = details.contains("templateFlags")
                               ? !details.value("template").toMap().isEmpty() && contains_ci(details.value("templateFlags").toStringList(), "Traits")
                               : summary.value("usesTemplate").toBool();
  const auto race = summary.value("raceFormKey").toString().toLower();
  s.race_uses_facegen = races_with_facegen.isEmpty() || race.isEmpty() || races_with_facegen.contains(race);
  s.chargen_preset = contains_ci(details.value("flags").toStringList(), "IsCharGenFacePreset");
  s.missing_masters = missing_masters(snapshot, s.winning_plugin);
  s.is_player = text::iequals_ascii(s.form_key, "000007:Skyrim.esm");
  if (details.contains("locations")) s.placed_references = static_cast<int>(details.value("locations").toList().size());

  const auto ctx = ResourceContext::for_snapshot(snapshot);
  s.fsmp_installed = snapshot->vfs.resolve("SKSE\\Plugins\\hdtSMP64.dll").has_value();
  s.observed_bald_in_game = observed_bald(s.form_key);
  s.hair_armors = hair_armors(snapshot, ctx.get(), details);
  for (const auto& v : details.value("headParts").toList()) {
    const auto m = v.toMap();
    const auto record = m.value("record").toMap();
    appearance::AuditHeadPart p;
    p.form_key = ss(record.value("formKey").toString());
    p.editor_id = ss(record.value("editorId").toString());
    p.type = ss(m.value("partType").toString());
    p.record_resolved = record.value("resolved", true).toBool();
    p.plugin = ss(record.value("winningPlugin").toString());
    if (p.plugin.empty()) {
      const auto colon = p.form_key.find(':');
      if (colon != std::string::npos) p.plugin = p.form_key.substr(colon + 1);
    }
    p.record_mod = plugin_mod(snapshot, p.plugin);
    p.model_path = mesh_data_path(ss(m.value("modelPath").toString()));
    if (!p.model_path.empty()) p.mesh = asset_source(snapshot, ctx.get(), p.model_path);
    s.head_parts.push_back(std::move(p));
  }

  // The head the game draws: the effective FaceGen's shapes, named after the head part EditorIDs. Read with the
  // same parser as the control map's head-part check (face::inspect_facegen), so both see the same shapes.
  if (inputs.facegen.found && inputs.facegen_readable != false && ctx && ctx->resolver) {
    if (const auto bytes = ctx->resolver->read(inputs.facegen.path)) {
      const auto info = face::inspect_facegen(*bytes);  // FACE_005 comes from the control map when unreadable
      if (info.readable) {
        entry.facegen_parsed = true;
        const auto has_shape = [&](const std::string& name) {
          return std::any_of(info.shapes.begin(), info.shapes.end(), [&](const auto& sh) { return text::iequals_ascii(sh.first, name); });
        };
        // Hair the FaceGen draws: HairTint shader shapes, plus shapes named "*hair*" (hairlines, unusual shaders).
        std::vector<std::string> hair_shapes;
        for (const auto& [name, color] : info.hair_tints) hair_shapes.push_back(name);
        for (const auto& [name, vertices] : info.shapes) {
          if (text::to_lower_ascii(name).find("hair") != std::string::npos &&
              std::none_of(hair_shapes.begin(), hair_shapes.end(), [&](const std::string& h) { return text::iequals_ascii(h, name); }))
            hair_shapes.push_back(name);
        }
        s.facegen_has_hair = !hair_shapes.empty();
        std::vector<std::string> record_names;
        std::string matched_hair;
        for (auto& p : s.head_parts) {
          if (!p.record_resolved || p.editor_id.empty()) continue;
          record_names.push_back(p.editor_id);
          if (p.model_path.empty()) continue;  // mesh-less placeholders and hair containers have no shape of their own
          p.in_facegen = has_shape(p.editor_id);
          if (*p.in_facegen && text::iequals_ascii(p.type, "Hair") && matched_hair.empty()) matched_hair = p.editor_id;
        }
        for (const auto& h : hair_shapes) {
          if (std::none_of(record_names.begin(), record_names.end(), [&](const std::string& r) { return text::iequals_ascii(r, h); }))
            s.facegen_foreign_hair_shapes.push_back(h);
        }
        // One full parse of the FaceGen when it draws hair: the textures the game loads for the record's hair (CK
        // bakes the HDPT texture set into the FaceGen) and the SMP state of every hair shape.
        if (!hair_shapes.empty()) {
          try {
            const auto mesh = nif::load_mesh(*bytes, inputs.facegen.path);
            const auto shape_named = [&](const std::string& name) {
              return std::find_if(mesh.shapes.begin(), mesh.shapes.end(), [&](const nif::Shape& sh) { return text::iequals_ascii(sh.name, name); });
            };
            if (!matched_hair.empty()) {
              const auto shape = shape_named(matched_hair);
              auto part = std::find_if(s.head_parts.begin(), s.head_parts.end(), [&](const appearance::AuditHeadPart& p) { return p.editor_id == matched_hair; });
              if (shape != mesh.shapes.end() && part != s.head_parts.end()) {
                for (const auto slot : {0, 1}) {  // diffuse and normal map
                  if (!shape->textures[slot].empty()) part->textures.push_back(asset_source(snapshot, ctx.get(), shape->textures[slot]));
                }
              }
            }
            for (const auto& name : hair_shapes) {
              const auto shape = shape_named(name);
              if (shape != mesh.shapes.end()) {
                entry.facegen_hair_vertices = std::max(entry.facegen_hair_vertices, static_cast<int>(shape->positions.size()));
                if (auto smp = smp_hair_facts(snapshot, ctx.get(), mesh, *shape, s.head_parts)) s.smp_facegen.push_back(std::move(*smp));
              }
            }
          } catch (const std::exception&) {
            // Shape names were read; only the texture and SMP checks are skipped for this NPC.
          }
        }
      }
    }
  }
  entry.audit = appearance::audit_npc_appearance(s, inputs, report);
  return entry;
}

QVariantMap npc_health_row(const NpcHealthEntry& entry, const QVariantMap& details, const appearance::ControlInputs& inputs) {
  const auto& s = entry.subject;
  const auto& a = entry.audit;
  QVariantList findings;
  QStringList codes;
  for (const auto& f : a.findings) {
    codes << qs(f.code);
    findings << QVariantMap{{"code", qs(f.code)},         {"title", qs(f.title)},       {"status", qs(f.status)},
                            {"severity", qs(f.severity)}, {"confidence", f.confidence}, {"evidence", qs(f.evidence)},
                            {"cause", qs(f.cause)},       {"action", qs(f.action)},     {"verification", qs(f.verification)},
                            {"resource", qs(f.resource)}, {"responsible", qs(f.responsible)}, {"controlCode", qs(f.control_code)}};
  }
  QVariantList parts;
  for (const auto& p : s.head_parts) {
    QVariantList textures;
    for (const auto& t : p.textures) textures << asset_map(t);
    parts << QVariantMap{{"formKey", qs(p.form_key)}, {"editorId", qs(p.editor_id)}, {"type", qs(p.type)}, {"resolved", p.record_resolved},
                         {"plugin", qs(p.plugin)}, {"recordMod", qs(p.record_mod)}, {"mesh", asset_map(p.mesh)}, {"textures", textures},
                         {"inFaceGen", p.in_facegen ? QVariant(*p.in_facegen) : QVariant()}};
  }
  QVariantList wigs;
  for (const auto& w : s.hair_armors) {
    QVariantList meshes, physics;
    for (const auto& m : w.meshes) meshes << asset_map(m);
    for (const auto& x : w.physics) physics << asset_map(x);
    wigs << QVariantMap{{"formKey", qs(w.form_key)}, {"editorId", qs(w.editor_id)}, {"plugin", qs(w.plugin)}, {"recordMod", qs(w.record_mod)},
                        {"smp", w.smp}, {"addonsForRace", w.addons_for_race ? QVariant(*w.addons_for_race) : QVariant()},
                        {"meshes", meshes}, {"physics", physics}};
  }
  QVariantList smp;
  for (const auto& h : s.smp_facegen) {
    smp << QVariantMap{{"shape", qs(h.shape)}, {"sourceMesh", qs(h.source_mesh)}, {"physicsBones", to_qstrings(h.physics_bones)},
                       {"sourcePhysicsXml", qs(h.source_physics_xml)}, {"faceGenHasPhysicsXml", h.facegen_has_physics_xml},
                       {"physicsXml", h.physics_xml ? asset_map(*h.physics_xml) : QVariantMap{}},
                       {"maxBoneOffset", h.max_bone_offset ? QVariant(*h.max_bone_offset) : QVariant()},
                       {"bonesMissingInSource", to_qstrings(h.bones_missing_in_source)}};
  }
  const auto summary = details.value("summary").toMap();
  const auto name = summary.value("name").toString();
  // Placed references (ACHR): the RefID the console shows when the owner clicks the NPC in game maps back to this row.
  QStringList references;
  for (const auto& l : details.value("locations").toList()) references << l.toMap().value("formKey").toString();
  QStringList outfit_armors;
  const auto outfit = details.value("defaultOutfit").toMap();
  for (const auto& armor : outfit.value("armors").toList()) outfit_armors << armor.toMap().value("formKey").toString();
  return {{"outfitArmors", outfit_armors},
          {"outfitOtherEntries", outfit.value("otherEntries").toList().size()},
          {"faceGenHairVertices", entry.facegen_hair_vertices},
          {"references", references},
          {"hairArmors", wigs},
          {"smpFaceGen", smp},
          {"fsmpInstalled", s.fsmp_installed ? QVariant(*s.fsmp_installed) : QVariant()},
          {"observedBaldInGame", s.observed_bald_in_game},
          {"race", details.value("race").toMap().value("editorId")},
          {"formKey", qs(s.form_key)},
          {"formId", summary.value("formId")},
          {"editorId", qs(s.editor_id)},
          {"name", name.isEmpty() ? qs(s.editor_id) : name},
          {"definingPlugin", qs(s.defining_plugin)},
          {"winningPlugin", qs(s.winning_plugin)},
          {"status", qs(a.status)},
          {"exemptions", to_qstrings(a.exemptions)},
          {"faceGenRegenerationSafe", a.facegen_regeneration_safe},
          {"faceGenParsed", entry.facegen_parsed},
          {"faceGenHasHair", s.facegen_has_hair ? QVariant(*s.facegen_has_hair) : QVariant()},
          {"faceGenForeignHair", to_qstrings(s.facegen_foreign_hair_shapes)},
          {"faceGeom", asset_map(inputs.facegen)},
          {"faceTint", asset_map(inputs.facetint)},
          {"headParts", parts},
          {"missingMasters", to_qstrings(s.missing_masters)},
          {"placedReferences", s.placed_references ? QVariant(*s.placed_references) : QVariant()},
          {"codes", codes},
          {"findings", findings},
          // Nothing here has been seen in game; the panel and the reports say so.
          {"inGameVerified", false}};
}

}  // namespace sns::app
