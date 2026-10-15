#include "sns/face/facegen.hpp"

#include <NifFile.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <sstream>
#include <unordered_map>

#include "sns/core/error.hpp"
#include "sns/core/text.hpp"

namespace sns::face {

namespace {

constexpr const char* kFaceGenNode = "BSFaceGenNiNodeSkinned";
constexpr std::uint32_t kShaderFaceTint = 4;
constexpr std::uint32_t kShaderHairTint = 6;
constexpr std::uint32_t kFaceTintSlot = 6;
constexpr std::uint32_t kNodeFlags = 14;  // what vanilla FaceGen roots and bone nodes carry

using Positions = std::vector<nifly::Vector3>;

// "meshes\Actors\X.tri" and "Actors\X.tri" name the same TRI: RaceMenu stores hosts without the
// "meshes\" prefix and morphs.ini does the same.
std::string host_key(std::string_view path) {
  auto key = text::data_path_key(path);
  if (key.rfind("meshes\\", 0) == 0) key.erase(0, 7);
  return key;
}

std::unique_ptr<nifly::NifFile> parse_nif(const std::string& bytes) {
  std::istringstream in(bytes, std::ios::in | std::ios::binary);
  auto nif = std::make_unique<nifly::NifFile>();
  if (nif->Load(in) != 0 || !nif->IsValid()) return nullptr;
  return nif;
}

std::optional<std::string> serialize_nif(nifly::NifFile& nif) {
  std::ostringstream stream(std::ios::out | std::ios::binary);
  if (nif.Save(stream) != 0) return std::nullopt;
  return std::move(stream).str();
}

bool is_drawn(nifly::NifFile& nif, nifly::NiShape* shape) {
  return nif.GetShader(shape) != nullptr && (shape->flags & 1u) == 0;
}

void add(Positions& p, const TriMorph& morph, float weight) {
  const auto n = std::min(p.size(), morph.deltas.size());
  for (std::size_t i = 0; i < n; ++i) {
    p[i].x += morph.deltas[i][0] * weight;
    p[i].y += morph.deltas[i][1] * weight;
    p[i].z += morph.deltas[i][2] * weight;
  }
}

std::string fmt_weight(const std::string& prefix, const std::string& name, float w) {
  return std::format("{}:{}={:.3f}", prefix, name, w);
}

void issue(std::vector<FaceIssue>& issues, std::string severity, std::string code, std::string message, std::string subject = {}) {
  issues.push_back({std::move(severity), std::move(code), std::move(message), std::move(subject)});
}

// The Creation Kit stores twice the CLFM color, and black when the NPC has no hair color (both
// measured on vanilla FaceGen).
nifly::Vector3 hair_tint_vector(const std::optional<Rgb8>& c) {
  if (!c) return nifly::Vector3(0.0f, 0.0f, 0.0f);
  return nifly::Vector3(2.0f * c->r / 255.0f, 2.0f * c->g / 255.0f, 2.0f * c->b / 255.0f);
}

// Head parts the game colors with the NPC's hair color (through a HairTint lighting shader).
bool is_hair_colored(HeadPartType type) {
  return type == HeadPartType::Hair || type == HeadPartType::Eyebrows || type == HeadPartType::FacialHair;
}

void hair_tint_unsupported(std::vector<FaceIssue>& issues, const HeadPartData& part, nifly::NiShader* shader) {
  std::string kind = "no shader";
  if (auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(shader)) {
    kind = "BSLightingShaderProperty type " + std::to_string(lighting->GetShaderType());
  } else if (shader) {
    kind = shader->GetBlockName();
  }
  issue(issues, "warning", "HairTintUnsupported",
        part.editor_id + ": its shader (" + kind + ") is not HairTint, so the hair color does not change this piece "
                         "in game or in the preview",
        part.model);
}

// Morph weights by name, so that two plans can be subtracted (PreserveExisting).
struct WeightTable {
  std::string race;
  std::map<std::string, float> chargen, expression, custom;
};

WeightTable table_of(const MorphPlan& plan) {
  WeightTable t;
  t.race = plan.race_morph;
  for (const auto& w : plan.chargen) t.chargen[w.name] += w.weight;
  for (const auto& w : plan.expression) t.expression[w.name] += w.weight;
  for (const auto& w : plan.custom) t.custom[w.name] += w.weight;
  return t;
}

std::map<std::string, float> difference(const std::map<std::string, float>& a, const std::map<std::string, float>& b) {
  std::map<std::string, float> out = a;
  for (const auto& [name, w] : b) out[name] -= w;
  std::erase_if(out, [](const auto& kv) { return std::abs(kv.second) < 1e-6f; });
  return out;
}

using SculptMap = std::map<std::string, const SculptTarget*>;
SculptMap sculpt_by_host(const FaceValues& values) {
  SculptMap out;
  for (const auto& s : values.sculpt) out[host_key(s.host)] = &s;
  return out;
}

// Applies `weights` (race morph weight given separately) to the positions of one head part.
struct PartMorphInput {
  float race_weight = 0.0f;
  std::string race_morph;
  std::map<std::string, float> chargen, expression, custom;
  const SculptTarget* sculpt_add = nullptr;
  const SculptTarget* sculpt_remove = nullptr;
};

void morph_part(const HeadPartData& part, const FaceRecipe& recipe, FaceAssetCache& assets, const PartMorphInput& in,
                Positions& positions, PartBuild& report, std::vector<FaceIssue>& issues,
                std::map<std::string, bool>& custom_found) {
  std::string chargen_host;
  for (const auto& ref : part.tris) {
    std::shared_ptr<const TriFile> tri;
    try {
      tri = assets.tri(ref.path);
    } catch (const std::exception& e) {
      issue(issues, "error", "TriUnreadable", e.what(), ref.path);
      continue;
    }
    if (!tri) {
      // A missing expression TRI only matters for SkinnyMorph; race/chargen ones change the shape.
      const bool needed = ref.kind != TriKind::Expression || !in.expression.empty();
      if (needed) {
        issue(issues, ref.kind == TriKind::Expression ? "warning" : "error", "TriMissing",
              part.editor_id + ": morph file not found in the active profile", ref.path);
      }
      continue;
    }
    if (tri->vertex_count != positions.size()) {
      // Only a problem when this TRI has something to apply. Replacer heads (e.g. a high-poly head
      // that still names the vanilla TRI files) keep their exported FaceGen untouched when only
      // colors change: PreserveExisting then asks for no morph at all.
      const bool needed = ref.kind == TriKind::Race       ? in.race_weight != 0.0f && !in.race_morph.empty()
                          : ref.kind == TriKind::Chargen  ? !in.chargen.empty()
                                                          : !in.expression.empty();
      if (needed) {
        issue(issues, "error", "TriVertexMismatch",
              std::format("{}: TRI has {} vertices, the mesh has {}; morphs cannot be applied", part.editor_id,
                          tri->vertex_count, positions.size()),
              ref.path);
      } else {
        issue(issues, "info", "TriVertexMismatchUnused",
              std::format("{}: TRI has {} vertices, the mesh has {}; no morph of this file is needed by the edit, "
                          "slider changes on this head part would not be possible",
                          part.editor_id, tri->vertex_count, positions.size()),
              ref.path);
      }
      continue;
    }
    auto apply = [&](const std::map<std::string, float>& weights, const char* prefix) {
      for (const auto& [name, w] : weights) {
        if (const auto* morph = tri->find(name)) {
          add(positions, *morph, w);
          report.applied.push_back(fmt_weight(prefix, name, w));
        } else if (part.type == HeadPartType::Face) {
          report.missing.push_back(name);
        }
      }
    };
    switch (ref.kind) {
      case TriKind::Race:
        if (in.race_weight != 0.0f && !in.race_morph.empty()) {
          if (const auto* morph = tri->find(in.race_morph)) {
            add(positions, *morph, in.race_weight);
            report.applied.push_back(fmt_weight("race", in.race_morph, in.race_weight));
          } else {
            report.missing.push_back(in.race_morph);
          }
        }
        break;
      case TriKind::Chargen:
        chargen_host = host_key(ref.path);
        apply(in.chargen, "chargen");
        break;
      case TriKind::Expression:
        apply(in.expression, "expression");
        break;
    }
  }

  if (chargen_host.empty()) return;
  // RaceMenu extension morphs registered for this part's chargen TRI.
  if (!in.custom.empty()) {
    for (const auto& ext : recipe.morph_extensions) {
      if (host_key(ext.host) != chargen_host) continue;
      std::shared_ptr<const TriFile> tri;
      try {
        tri = assets.tri(ext.tri);
      } catch (const std::exception& e) {
        issue(issues, "warning", "TriUnreadable", e.what(), ext.tri);
        continue;
      }
      if (!tri || tri->vertex_count != positions.size()) continue;
      for (const auto& [name, w] : in.custom) {
        if (const auto* morph = tri->find(name)) {
          add(positions, *morph, w);
          custom_found[name] = true;
          report.applied.push_back(fmt_weight("custom", name, w));
        }
      }
    }
  }
  auto apply_sculpt = [&](const SculptTarget* s, float sign) {
    if (!s) return;
    if (s->vertex_count != positions.size()) {
      issue(issues, "error", "SculptVertexMismatch",
            std::format("{}: sculpt data was made for {} vertices, the mesh has {}", part.editor_id, s->vertex_count,
                        positions.size()),
            s->host);
      return;
    }
    for (const auto& [index, d] : s->deltas) {
      if (index >= positions.size()) continue;
      positions[index].x += sign * d[0];
      positions[index].y += sign * d[1];
      positions[index].z += sign * d[2];
    }
    report.applied.push_back(std::format("sculpt:{}{}", sign < 0 ? "-" : "", s->deltas.size()));
  };
  apply_sculpt(in.sculpt_add, 1.0f);
  apply_sculpt(in.sculpt_remove, -1.0f);
}

void move_under(nifly::NifFile& nif, nifly::NiNode* from, nifly::NiNode* to, std::uint32_t id) {
  std::vector<std::uint32_t> indices;
  from->childRefs.GetIndices(indices);
  std::erase(indices, id);
  from->childRefs.SetIndices(indices);
  to->childRefs.AddBlockRef(id);
  (void)nif;
}

std::string root_name_for(const FaceRecipe& recipe) {
  auto name = recipe.facegen_mesh_path;
  if (const auto slash = name.find_last_of("\\/"); slash != std::string::npos) name.erase(0, slash + 1);
  for (auto& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return name.empty() ? std::string("FACEGEN.NIF") : name;
}

}  // namespace

// ------------------------------------------------------------------------------------- plans

MorphPlan plan_morphs(const FaceRecipe& recipe) {
  MorphPlan plan;
  plan.race_morph = race_morph_name(recipe);
  const auto& v = recipe.values;
  const auto& slots = face_preset_slots();
  for (std::size_t i = 0; i < slots.size(); ++i) {
    if (!slots[i].prefix.empty() && v.presets[i] > 0) plan.chargen.push_back({slots[i].prefix + std::to_string(v.presets[i]), 1.0f});
  }
  const auto& sliders = get_standard_face_morphs();
  for (const auto& def : sliders) {
    const float x = std::clamp(v.sliders[def.index], -1.0f, 1.0f);
    if (x > 0.0f) plan.chargen.push_back({def.positive_morph, x});
    else if (x < 0.0f) plan.chargen.push_back({def.negative_morph, -x});
  }
  if (recipe.vampire) plan.chargen.push_back({"VampireMorph", 1.0f});
  const float weight = std::clamp(v.weight, 0.0f, 100.0f);
  if (weight < 100.0f) plan.expression.push_back({"SkinnyMorph", 1.0f - weight / 100.0f});
  for (const auto& [name, w] : v.custom_morphs) {
    if (w != 0.0f) plan.custom.push_back({name, w});
  }
  return plan;
}

// ------------------------------------------------------------------------------------- cache

FaceAssetCache::FaceAssetCache(AssetReader reader, std::size_t max_bytes) : reader_(std::move(reader)), max_bytes_(max_bytes) {}

std::shared_ptr<const std::string> FaceAssetCache::bytes(std::string_view data_path) {
  const auto key = text::data_path_key(data_path);
  {
    std::lock_guard lock(mutex_);
    if (const auto it = bytes_.find(key); it != bytes_.end()) return it->second;
  }
  auto data = reader_ ? reader_(key) : std::nullopt;
  std::shared_ptr<const std::string> value = data ? std::make_shared<const std::string>(std::move(*data)) : nullptr;
  std::lock_guard lock(mutex_);
  const auto size = value ? value->size() : 0;
  if (cached_bytes_ + size > max_bytes_) {
    bytes_.clear();
    cached_bytes_ = 0;
  }
  cached_bytes_ += size;
  bytes_[key] = value;
  return value;
}

std::shared_ptr<const TriFile> FaceAssetCache::tri(std::string_view data_path) {
  const auto key = text::data_path_key(data_path);
  {
    std::lock_guard lock(mutex_);
    if (const auto it = tris_.find(key); it != tris_.end()) return it->second;
  }
  const auto raw = bytes(key);
  std::shared_ptr<const TriFile> parsed = raw ? std::make_shared<const TriFile>(parse_tri(*raw, key)) : nullptr;
  std::lock_guard lock(mutex_);
  tris_[key] = parsed;
  return parsed;
}

void FaceAssetCache::clear() {
  std::lock_guard lock(mutex_);
  bytes_.clear();
  tris_.clear();
  cached_bytes_ = 0;
}

// ------------------------------------------------------------------------------------- build

bool FaceGenBuild::exportable() const {
  return !nif.empty() && std::none_of(issues.begin(), issues.end(), [](const auto& i) { return i.severity == "error"; });
}

FaceGenBuild build_facegen(const FaceRecipe& recipe, FaceAssetCache& assets, const FaceGenBuildOptions& options) {
  FaceGenBuild build;
  if (recipe.parts.empty()) {
    issue(build.issues, "error", "NoHeadParts", "The NPC has no head parts: there is nothing to build", recipe.npc_form_key);
    return build;
  }

  // PreserveExisting needs the existing file and the values it was made with.
  std::unique_ptr<nifly::NifFile> existing;
  if (options.mode == GeometryMode::PreserveExisting) {
    if (options.existing_facegen) existing = parse_nif(*options.existing_facegen);
    if (!existing || !options.baseline) {
      issue(build.issues, "warning", "PreserveUnavailable",
            "The existing FaceGen could not be used as a base; the head was regenerated from its parts", recipe.npc_form_key);
      existing.reset();
    }
  }

  const auto plan = table_of(plan_morphs(recipe));
  WeightTable delta;
  if (existing) {
    auto base_recipe = recipe;
    base_recipe.values = *options.baseline;
    const auto base = table_of(plan_morphs(base_recipe));
    delta.chargen = difference(plan.chargen, base.chargen);
    delta.expression = difference(plan.expression, base.expression);
    delta.custom = difference(plan.custom, base.custom);
  }
  const auto sculpt_new = sculpt_by_host(recipe.values);
  const auto sculpt_old = existing ? sculpt_by_host(*options.baseline) : SculptMap{};

  nifly::NifFile out;
  out.Create(nifly::NiVersion::getSSE());
  auto& hdr = out.GetHeader();
  {
    auto fade = std::make_unique<nifly::BSFadeNode>();
    fade->name.get() = root_name_for(recipe);
    fade->flags = kNodeFlags;
    hdr.ReplaceBlock(0, std::move(fade));
  }
  auto* root = out.GetRootNode();
  auto node_block = std::make_unique<nifly::NiNode>();
  node_block->name.get() = kFaceGenNode;
  node_block->flags = kNodeFlags;
  const auto node_id = hdr.AddBlock(std::move(node_block));
  root->childRefs.AddBlockRef(node_id);
  auto* facegen_node = hdr.GetBlock<nifly::NiNode>(node_id);

  std::map<std::string, bool> custom_found;
  for (const auto& [name, w] : plan.custom) custom_found[name] = false;

  for (const auto& part : recipe.parts) {
    // Cooperative cancellation: a newer edit superseded this build; stop between head parts.
    if (options.cancelled && options.cancelled()) {
      build.cancelled = true;
      build.nif.clear();
      issue(build.issues, "info", "BuildCancelled", "Superseded by a newer edit before it finished", recipe.npc_form_key);
      return build;
    }
    PartBuild report;
    report.editor_id = part.editor_id;
    report.model = part.model;
    if (part.model.empty()) {
      report.status = "no-model";
      build.parts.push_back(std::move(report));
      continue;
    }

    // Source geometry: the part's own mesh, or the matching shape of the existing FaceGen.
    std::unique_ptr<nifly::NifFile> mesh;
    nifly::NifFile* source = nullptr;
    nifly::NiShape* shape = nullptr;
    bool preserved = false;

    const auto raw = assets.bytes(part.model);
    if (raw) mesh = parse_nif(*raw);
    std::size_t mesh_vertices = 0;
    nifly::NiShape* mesh_shape = nullptr;
    if (mesh) {
      std::size_t drawn = 0;
      for (auto* s : mesh->GetShapes()) {
        if (!is_drawn(*mesh, s)) continue;
        ++drawn;
        if (!mesh_shape) mesh_shape = s;
      }
      if (!mesh_shape && !mesh->GetShapes().empty()) mesh_shape = mesh->GetShapes().front();
      if (drawn > 1) {
        issue(build.issues, "warning", "HeadPartMultipleShapes",
              std::format("{}: the mesh has {} drawn shapes; FaceGen takes the first one ('{}'), as for vanilla head parts",
                          part.editor_id, drawn, mesh_shape->name.get()),
              part.model);
      }
      if (mesh_shape) mesh_vertices = mesh_shape->GetNumVertices();
      if (!mesh->GetHeader().GetVersion().IsSSE()) {
        issue(build.issues, "error", "HeadPartMeshNotSse", part.editor_id + ": the mesh is not a Skyrim SE NIF", part.model);
        mesh_shape = nullptr;
      }
    }

    if (existing) {
      for (auto* s : existing->GetShapes()) {
        if (text::iequals_ascii(s->name.get(), part.editor_id) && (!mesh_shape || s->GetNumVertices() == mesh_vertices)) {
          source = existing.get();
          shape = s;
          preserved = true;
          break;
        }
      }
    }
    if (!shape) {
      if (!raw) {
        report.status = "missing-mesh";
        issue(build.issues, "error", "HeadPartMeshMissing", part.editor_id + ": mesh not found in the active profile", part.model);
        build.parts.push_back(std::move(report));
        continue;
      }
      if (!mesh) {
        report.status = "unreadable-mesh";
        issue(build.issues, "error", "HeadPartMeshUnreadable", part.editor_id + ": the mesh is not a readable NIF", part.model);
        build.parts.push_back(std::move(report));
        continue;
      }
      if (!mesh_shape) {
        report.status = "no-shape";
        issue(build.issues, "error", "HeadPartNoShape", part.editor_id + ": the mesh has no usable shape", part.model);
        build.parts.push_back(std::move(report));
        continue;
      }
      source = mesh.get();
      shape = mesh_shape;
    }

    Positions positions;
    source->GetVertsForShape(shape, positions);
    PartMorphInput input;
    if (preserved) {
      input.chargen = delta.chargen;
      input.expression = delta.expression;
      input.custom = delta.custom;
      const auto host = [&]() {
        for (const auto& t : part.tris) {
          if (t.kind == TriKind::Chargen) return host_key(t.path);
        }
        return std::string();
      }();
      if (const auto it = sculpt_new.find(host); it != sculpt_new.end()) input.sculpt_add = it->second;
      if (const auto it = sculpt_old.find(host); it != sculpt_old.end()) input.sculpt_remove = it->second;
      if (input.sculpt_add && input.sculpt_remove && *input.sculpt_add == *input.sculpt_remove) {
        input.sculpt_add = input.sculpt_remove = nullptr;
      }
    } else {
      input.race_weight = 1.0f;
      input.race_morph = plan.race;
      input.chargen = plan.chargen;
      input.expression = plan.expression;
      input.custom = plan.custom;
      for (const auto& t : part.tris) {
        if (t.kind != TriKind::Chargen) continue;
        if (const auto it = sculpt_new.find(host_key(t.path)); it != sculpt_new.end()) input.sculpt_add = it->second;
      }
    }
    morph_part(part, recipe, assets, input, positions, report, build.issues, custom_found);

    auto* clone = out.CloneShape(shape, part.editor_id, source);
    if (!clone) {
      report.status = "no-shape";
      issue(build.issues, "error", "HeadPartCloneFailed", part.editor_id + ": the shape could not be copied", part.model);
      build.parts.push_back(std::move(report));
      continue;
    }
    move_under(out, root, facegen_node, out.GetBlockID(clone));
    out.SetVertsForShape(clone, positions);
    clone->UpdateBounds();

    auto* shader = out.GetShader(clone);
    if (!preserved && part.texture_set) {
      for (std::uint32_t slot = 0; slot < 9; ++slot) {
        auto path = (*part.texture_set)[slot];
        if (!path.empty()) out.SetTextureSlot(clone, path, slot);
      }
    }
    if (shader && shader->GetShaderType() == kShaderFaceTint && part.type == HeadPartType::Face) {
      if (!preserved && recipe.face_texture_set) {
        for (std::uint32_t slot = 0; slot < 9; ++slot) {
          auto path = (*recipe.face_texture_set)[slot];
          if (!path.empty() && slot != kFaceTintSlot) out.SetTextureSlot(clone, path, slot);
        }
      }
      auto tint = recipe.facegen_tint_path;
      out.SetTextureSlot(clone, tint, kFaceTintSlot);
    }
    if (auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(shader);
        lighting && lighting->GetShaderType() == kShaderHairTint) {
      lighting->hairTintColor = hair_tint_vector(recipe.values.hair_color);
    } else if (is_hair_colored(part.type)) {
      hair_tint_unsupported(build.issues, part, shader);
    }

    report.status = "built";
    report.geometry_source = preserved ? "preserved" : "regenerated";
    report.vertex_count = positions.size();
    build.parts.push_back(std::move(report));
  }

  for (const auto& [name, found] : custom_found) {
    if (!found) {
      issue(build.issues, "warning", "CustomMorphUnresolved",
            "RaceMenu morph '" + name + "' is not defined by any morphs.ini TRI of these head parts; it is not baked", name);
    }
  }

  // Bone nodes are cloned under the root by CloneShape; give them the vanilla node flags.
  std::vector<std::uint32_t> children;
  root->childRefs.GetIndices(children);
  for (const auto id : children) {
    if (auto* n = hdr.GetBlock<nifly::NiNode>(id); n && id != node_id) n->flags = kNodeFlags;
  }

  if (std::none_of(build.parts.begin(), build.parts.end(), [](const auto& p) { return p.status == "built"; })) {
    issue(build.issues, "error", "FaceGenEmpty", "No head part could be built", recipe.npc_form_key);
    return build;
  }
  std::ostringstream stream(std::ios::out | std::ios::binary);
  if (out.Save(stream) != 0) {
    issue(build.issues, "error", "FaceGenWriteFailed", "nifly could not serialize the FaceGen NIF", recipe.npc_form_key);
    return build;
  }
  build.nif = std::move(stream).str();
  return build;
}

// ------------------------------------------------------------------------------------- inspection

FaceGenInfo inspect_facegen(std::string_view nif_bytes) {
  FaceGenInfo info;
  const auto nif = parse_nif(std::string(nif_bytes));
  if (!nif) return info;
  info.readable = true;
  if (auto* root = nif->GetRootNode()) {
    info.root_type = root->GetBlockName();
    info.root_name = root->name.get();
  }
  for (auto* shape : nif->GetShapes()) {
    info.shapes.emplace_back(shape->name.get(), shape->GetNumVertices());
    if (auto* parent = nif->GetParentNode(shape); parent && parent->name.get() == kFaceGenNode) info.has_facegen_node = true;
    if (auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(nif->GetShader(shape));
        lighting && lighting->GetShaderType() == kShaderHairTint) {
      info.hair_tints.push_back({shape->name.get(), {lighting->hairTintColor.x, lighting->hairTintColor.y, lighting->hairTintColor.z}});
    }
    if (auto* shader = nif->GetShader(shape); shader && shader->GetShaderType() == kShaderFaceTint) {
      std::string tex;
      nif->GetTextureSlot(shape, tex, kFaceTintSlot);
      info.face_tint_slot = tex;
    }
  }
  return info;
}

namespace {

// Where each HairTint color lives in a NIF file: the last 12 bytes (three little-endian floats) of a
// BSLightingShaderProperty block of shader type 6 (nifly BSLightingShaderProperty::Sync and nif.xml:
// "Hair Tint Color" is the last field for Skyrim/SSE). Block offsets come from the header's block
// sizes, counted back from the end of the file (footer: root count + root references). Every slot
// is checked against the value nifly read; any disagreement means the layout is not what this code
// expects, and nothing is patched.
struct HairTintSlot {
  std::size_t offset = 0;
  std::vector<std::string> shapes;
};

// Size of the header of a 20.2.0.7 NIF (Skyrim LE/SE): the first block starts right after it.
// Read forward, because some FaceGen files carry leftover bytes after their footer (a longer file
// rewritten without truncation; the game ignores them), which breaks counting back from the end.
std::optional<std::size_t> nif_header_size(std::string_view b) {
  const auto newline = b.find('\n');
  if (newline == std::string_view::npos) return std::nullopt;
  std::size_t p = newline + 1;
  auto skip = [&](std::size_t n) {
    if (n > b.size() || p > b.size() - n) return false;
    p += n;
    return true;
  };
  auto u32 = [&](std::uint32_t& v) {
    if (p + 4 > b.size()) return false;
    std::memcpy(&v, b.data() + p, 4);
    p += 4;
    return true;
  };
  std::uint32_t version = 0, user = 0, blocks = 0, bsver = 0;
  if (!u32(version) || version != 0x14020007 || !skip(1) || !u32(user) || !u32(blocks) || !u32(bsver) || bsver >= 130) {
    return std::nullopt;
  }
  for (int i = 0; i < 3; ++i) {  // author, process script, export script: byte length + text
    if (p >= b.size() || !skip(1 + static_cast<unsigned char>(b[p]))) return std::nullopt;
  }
  if (p + 2 > b.size()) return std::nullopt;
  std::uint16_t types = 0;
  std::memcpy(&types, b.data() + p, 2);
  p += 2;
  for (std::uint16_t i = 0; i < types; ++i) {
    std::uint32_t n = 0;
    if (!u32(n) || !skip(n)) return std::nullopt;
  }
  if (!skip(std::size_t{2} * blocks) || !skip(std::size_t{4} * blocks)) return std::nullopt;  // type indices, sizes
  std::uint32_t strings = 0, max_length = 0;
  if (!u32(strings) || !u32(max_length)) return std::nullopt;
  for (std::uint32_t i = 0; i < strings; ++i) {
    std::uint32_t n = 0;
    if (!u32(n) || !skip(n)) return std::nullopt;
  }
  std::uint32_t groups = 0;
  if (!u32(groups) || !skip(std::size_t{4} * groups)) return std::nullopt;
  return p;
}

std::optional<std::vector<HairTintSlot>> hair_tint_slots(nifly::NifFile& nif, std::string_view bytes, std::string& why) {
  auto& hdr = nif.GetHeader();
  const auto blocks = hdr.GetNumBlocks();
  std::vector<std::size_t> offsets(blocks + 1, 0);
  std::size_t total = 0;
  for (std::uint32_t i = 0; i < blocks; ++i) {
    const auto size = hdr.GetBlockSize(i);
    if (size == nifly::NIF_NPOS) {
      why = "the header has no size for block " + std::to_string(i);
      return std::nullopt;
    }
    offsets[i] = total;
    total += size;
  }
  const auto footer = 4 + 4 * hdr.GetRootBlockIds().size();
  if (total + footer >= bytes.size()) {
    why = "block sizes do not fit in the file";
    return std::nullopt;
  }
  // The header read forward; counting back from the end of the file only when that is not possible.
  const auto forward = nif_header_size(bytes);
  const auto header_end = forward && *forward + total + footer <= bytes.size() ? *forward : bytes.size() - footer - total;
  std::vector<HairTintSlot> slots;
  for (auto* shape : nif.GetShapes()) {
    auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(nif.GetShader(shape));
    if (!lighting || lighting->GetShaderType() != kShaderHairTint) continue;
    const auto id = nif.GetBlockID(lighting);
    if (id == nifly::NIF_NPOS || id >= blocks || hdr.GetBlockSize(id) < 12) {
      why = "HairTint shader of '" + shape->name.get() + "' has no usable block";
      return std::nullopt;
    }
    const auto offset = header_end + offsets[id] + hdr.GetBlockSize(id) - 12;
    float stored[3];
    std::memcpy(stored, bytes.data() + offset, sizeof stored);
    const float parsed[3] = {lighting->hairTintColor.x, lighting->hairTintColor.y, lighting->hairTintColor.z};
    if (std::memcmp(stored, parsed, sizeof stored) != 0) {
      why = "the HairTint color of '" + shape->name.get() + "' is not at the expected position of its shader block";
      return std::nullopt;
    }
    const auto same = std::find_if(slots.begin(), slots.end(), [&](const HairTintSlot& s) { return s.offset == offset; });
    if (same != slots.end()) {
      same->shapes.push_back(shape->name.get());
    } else {
      slots.push_back({offset, {shape->name.get()}});
    }
  }
  return slots;
}

std::string vec_text(const nifly::Vector3& v) { return std::format("({:.6g}, {:.6g}, {:.6g})", v.x, v.y, v.z); }

std::string transform_text(const nifly::MatTransform& t) { return std::format("t{} s{:.6g}", vec_text(t.translation), t.scale); }

bool same_transform(const nifly::MatTransform& a, const nifly::MatTransform& b) {
  return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}

template <class T, class Eq>
std::size_t count_different(const std::vector<T>& a, const std::vector<T>& b, Eq eq) {
  if (a.size() != b.size()) return std::max(a.size(), b.size());
  std::size_t n = 0;
  for (std::size_t i = 0; i < a.size(); ++i) n += !eq(a[i], b[i]);
  return n;
}

// Semantic comparison of everything Hair Color Safe Mode must keep, shape by shape (matched by name,
// so a serializer that reorders equivalent blocks is not reported) plus the skeleton's nodes.
void compare_semantics(nifly::NifFile& a, nifly::NifFile& b, std::vector<FaceGenDifference>& out) {
  auto add = [&](std::string shape, std::string property, std::string before, std::string after) {
    out.push_back({std::move(shape), std::move(property), std::move(before), std::move(after)});
  };
  const auto shapes_a = a.GetShapes();
  const auto shapes_b = b.GetShapes();
  if (shapes_a.size() != shapes_b.size()) add("", "shape count", std::to_string(shapes_a.size()), std::to_string(shapes_b.size()));
  for (auto* sa : shapes_a) {
    const auto name = sa->name.get();
    const auto it = std::find_if(shapes_b.begin(), shapes_b.end(), [&](nifly::NiShape* s) { return s->name.get() == name; });
    if (it == shapes_b.end()) {
      add(name, "shape", "present", "missing");
      continue;
    }
    auto* sb = *it;
    if (std::string(sa->GetBlockName()) != sb->GetBlockName()) add(name, "block type", sa->GetBlockName(), sb->GetBlockName());
    if (sa->flags != sb->flags) add(name, "flags", std::to_string(sa->flags), std::to_string(sb->flags));
    if (!same_transform(sa->transform, sb->transform)) add(name, "transform", transform_text(sa->transform), transform_text(sb->transform));

    std::vector<nifly::Vector3> pa, pb;
    a.GetVertsForShape(sa, pa);
    b.GetVertsForShape(sb, pb);
    if (pa.size() != pb.size()) {
      add(name, "vertices", std::to_string(pa.size()), std::to_string(pb.size()));
    } else {
      float worst = 0.0f;
      std::size_t moved = 0;
      for (std::size_t i = 0; i < pa.size(); ++i) {
        if (pa[i] == pb[i]) continue;
        ++moved;
        worst = std::max(worst, (pa[i] - pb[i]).length());
      }
      if (moved) add(name, "positions", "original", std::format("{} vertices moved, up to {:.6g} units", moved, worst));
    }
    std::vector<nifly::Triangle> ta, tb;
    sa->GetTriangles(ta);
    sb->GetTriangles(tb);
    if (ta.size() != tb.size()) {
      add(name, "triangles", std::to_string(ta.size()), std::to_string(tb.size()));
    } else if (const auto n = count_different(ta, tb, [](const nifly::Triangle& x, const nifly::Triangle& y) { return x == y; })) {
      add(name, "indices", "original", std::to_string(n) + " triangles with other vertex indices");
    }
    const auto* na = a.GetNormalsForShape(sa);
    const auto* nb = b.GetNormalsForShape(sb);
    if (static_cast<bool>(na) != static_cast<bool>(nb)) {
      add(name, "normals", na ? "present" : "absent", nb ? "present" : "absent");
    } else if (na) {
      if (const auto n = count_different(*na, *nb, [](const nifly::Vector3& x, const nifly::Vector3& y) { return x == y; })) {
        add(name, "normals", "original", std::to_string(n) + " normals changed");
      }
    }
    std::vector<nifly::Vector2> ua, ub;
    a.GetUvsForShape(sa, ua);
    b.GetUvsForShape(sb, ub);
    if (const auto n = count_different(ua, ub, [](const nifly::Vector2& x, const nifly::Vector2& y) { return x.u == y.u && x.v == y.v; })) {
      add(name, "uv", std::to_string(ua.size()) + " coordinates", std::to_string(n) + " coordinates changed");
    }
    std::vector<nifly::Vector3> ga, gb;
    a.GetTangentsForShape(sa, ga);
    b.GetTangentsForShape(sb, gb);
    if (const auto n = count_different(ga, gb, [](const nifly::Vector3& x, const nifly::Vector3& y) { return x == y; })) {
      add(name, "tangents", std::to_string(ga.size()), std::to_string(n) + " changed");
    }
    std::vector<nifly::Color4> ca, cb;
    a.GetColorsForShape(sa, ca);
    b.GetColorsForShape(sb, cb);
    if (const auto n = count_different(ca, cb, [](const nifly::Color4& x, const nifly::Color4& y) {
          return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
        })) {
      add(name, "vertex colors", std::to_string(ca.size()), std::to_string(n) + " changed");
    }

    std::vector<std::string> bones_a, bones_b;
    a.GetShapeBoneList(sa, bones_a);
    b.GetShapeBoneList(sb, bones_b);
    if (bones_a != bones_b) {
      add(name, "skin bones", std::to_string(bones_a.size()) + " bones", std::to_string(bones_b.size()) + " bones (list differs)");
    } else {
      for (std::uint32_t bone = 0; bone < bones_a.size(); ++bone) {
        std::unordered_map<std::uint16_t, float> wa, wb;
        a.GetShapeBoneWeights(sa, bone, wa);
        b.GetShapeBoneWeights(sb, bone, wb);
        if (wa != wb) add(name, "bone weights", bones_a[bone], std::to_string(wb.size()) + " weighted vertices (differs)");
        nifly::MatTransform xa, xb;
        const bool ha = a.GetShapeTransformSkinToBone(sa, bone, xa);
        const bool hb = b.GetShapeTransformSkinToBone(sb, bone, xb);
        if (ha != hb || (ha && !same_transform(xa, xb))) add(name, "skin-to-bone transform", bones_a[bone], "differs");
      }
      nifly::MatTransform xa, xb;
      const bool ha = a.GetShapeTransformGlobalToSkin(sa, xa);
      const bool hb = b.GetShapeTransformGlobalToSkin(sb, xb);
      if (ha != hb || (ha && !same_transform(xa, xb))) {
        add(name, "global-to-skin transform", ha ? transform_text(xa) : "none", hb ? transform_text(xb) : "none");
      }
    }
    nifly::NiVector<nifly::BSDismemberSkinInstance::PartitionInfo> pia, pib;
    std::vector<int> tpa, tpb;
    const bool parts_a = a.GetShapePartitions(sa, pia, tpa);
    const bool parts_b = b.GetShapePartitions(sb, pib, tpb);
    bool same_parts = parts_a == parts_b && pia.size() == pib.size() && tpa == tpb;
    for (std::uint32_t i = 0; same_parts && i < pia.size(); ++i) same_parts = pia[i].partID == pib[i].partID && pia[i].flags == pib[i].flags;
    if (!same_parts) add(name, "partitions", std::to_string(pia.size()), std::to_string(pib.size()) + " (differs)");

    auto* sha = a.GetShader(sa);
    auto* shb = b.GetShader(sb);
    if (static_cast<bool>(sha) != static_cast<bool>(shb)) {
      add(name, "shader", sha ? sha->GetBlockName() : "none", shb ? shb->GetBlockName() : "none");
    } else if (sha) {
      if (sha->GetShaderType() != shb->GetShaderType()) {
        add(name, "shader type", std::to_string(sha->GetShaderType()), std::to_string(shb->GetShaderType()));
      }
      for (std::uint32_t slot = 0; slot < 9; ++slot) {
        std::string xa, xb;
        a.GetTextureSlot(sa, xa, slot);
        b.GetTextureSlot(sb, xb, slot);
        if (xa != xb) add(name, "texture slot " + std::to_string(slot), xa, xb);
      }
    }
    auto* aa = a.GetAlphaProperty(sa);
    auto* ab = b.GetAlphaProperty(sb);
    if (static_cast<bool>(aa) != static_cast<bool>(ab) || (aa && (aa->flags != ab->flags || aa->threshold != ab->threshold))) {
      add(name, "alpha", aa ? std::format("flags {} threshold {}", aa->flags, aa->threshold) : "none",
          ab ? std::format("flags {} threshold {}", ab->flags, ab->threshold) : "none");
    }
  }
  for (auto* node : a.GetNodes()) {
    const auto name = node->name.get();
    nifly::MatTransform ta, tb;
    if (!a.GetNodeTransformToParent(name, ta)) continue;
    if (!b.GetNodeTransformToParent(name, tb)) {
      add(name, "node", "present", "missing");
    } else if (!same_transform(ta, tb)) {
      add(name, "node transform", transform_text(ta), transform_text(tb));
    }
  }
}

}  // namespace

HairTintPatch patch_hair_tint(std::string_view facegen_nif, const FaceRecipe& recipe) {
  HairTintPatch patch;
  const auto nif = parse_nif(std::string(facegen_nif));
  if (!nif) {
    issue(patch.issues, "error", "FaceGenUnreadable", "The FaceGen to recolor is not a readable NIF", recipe.facegen_mesh_path);
    return patch;
  }
  std::string why;
  const auto slots = hair_tint_slots(*nif, facegen_nif, why);
  if (!slots) {
    issue(patch.issues, "error", "HAIR_TINT_LAYOUT_UNVERIFIED",
          "The HairTint color could not be located byte for byte (" + why + "); nothing was changed", recipe.facegen_mesh_path);
    return patch;
  }
  for (auto* shape : nif->GetShapes()) {
    auto* shader = nif->GetShader(shape);
    if (auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(shader); lighting && lighting->GetShaderType() == kShaderHairTint) {
      continue;
    }
    const auto part = std::find_if(recipe.parts.begin(), recipe.parts.end(),
                                   [&](const HeadPartData& p) { return text::iequals_ascii(p.editor_id, shape->name.get()); });
    if (part != recipe.parts.end() && is_hair_colored(part->type)) hair_tint_unsupported(patch.issues, *part, shader);
  }
  if (slots->empty()) {
    issue(patch.issues, "error", "HAIR_TINT_NOT_APPLIED",
          "No shape of this FaceGen uses the HairTint shader, so a CLFM hair color cannot change it. The FaceGen was left "
          "untouched; recoloring needs a texture or material edit of the hair",
          recipe.facegen_mesh_path);
    return patch;
  }
  // Copy of the original with only the tint floats rewritten: every other byte is the original's.
  const auto tint = hair_tint_vector(recipe.values.hair_color);
  const float floats[3] = {tint.x, tint.y, tint.z};
  std::string out(facegen_nif);
  for (const auto& slot : *slots) {
    std::memcpy(out.data() + slot.offset, floats, sizeof floats);
    patch.patched.insert(patch.patched.end(), slot.shapes.begin(), slot.shapes.end());
  }
  const auto integrity = verify_hair_tint_integrity(facegen_nif, out);
  if (!integrity.unchanged_except_hair_tint || !integrity.byte_identical_outside_hair_tint) {
    issue(patch.issues, "error", "FACEGEN_GEOMETRY_CHANGED", "Hair Color Safe Mode: " + integrity.summary(), recipe.facegen_mesh_path);
    patch.patched.clear();
    return patch;
  }
  // The rewritten file must read back with the requested color on every HairTint shape.
  const auto check = parse_nif(out);
  bool applied = static_cast<bool>(check);
  if (check) {
    for (auto* shape : check->GetShapes()) {
      auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(check->GetShader(shape));
      if (lighting && lighting->GetShaderType() == kShaderHairTint && !(lighting->hairTintColor == tint)) applied = false;
    }
  }
  if (!applied) {
    issue(patch.issues, "error", "HAIR_TINT_NOT_APPLIED", "The recolored FaceGen does not read back with the requested color",
          recipe.facegen_mesh_path);
    patch.patched.clear();
    return patch;
  }
  patch.nif = std::move(out);
  return patch;
}

std::string FaceGenIntegrity::summary() const {
  if (unchanged_except_hair_tint) return "only the HairTint color differs";
  if (differences.empty()) return readable ? "unknown NIF difference" : "unreadable FaceGen";
  const auto& d = differences.front();
  auto text = (d.shape.empty() ? std::string("file") : "'" + d.shape + "'") + ": " + d.property + " changed from " + d.before + " to " + d.after;
  if (differences.size() > 1) text += std::format(" (+{} more)", differences.size() - 1);
  return text;
}

FaceGenIntegrity verify_hair_tint_integrity(std::string_view original, std::string_view candidate) {
  FaceGenIntegrity result;
  auto before = parse_nif(std::string(original));
  auto after = parse_nif(std::string(candidate));
  if (!before || !after) {
    result.differences.push_back({"", "file", before ? "readable" : "unreadable", after ? "readable" : "unreadable"});
    return result;
  }
  result.readable = true;
  std::string why;
  const auto slots = hair_tint_slots(*before, original, why);

  // 1. Byte level: equal size and every differing byte inside a HairTint color of the original.
  if (slots && original.size() == candidate.size()) {
    bool outside = false;
    for (std::size_t i = 0; i < original.size() && !outside; ++i) {
      if (original[i] == candidate[i]) continue;
      outside = std::none_of(slots->begin(), slots->end(), [&](const HairTintSlot& s) { return i >= s.offset && i < s.offset + 12; });
    }
    result.byte_identical_outside_hair_tint = !outside;
  }

  // 2. Semantics, shape by shape (also explains what changed when the bytes differ elsewhere).
  compare_semantics(*before, *after, result.differences);

  // 3. Catch-all for properties not listed above: with the candidate's tints set back to the
  // original's, both files must serialize identically.
  if (!result.byte_identical_outside_hair_tint && result.differences.empty()) {
    for (auto* shape : after->GetShapes()) {
      auto* tinted = dynamic_cast<nifly::BSLightingShaderProperty*>(after->GetShader(shape));
      if (!tinted || tinted->GetShaderType() != kShaderHairTint) continue;
      for (auto* other : before->GetShapes()) {
        auto* source = dynamic_cast<nifly::BSLightingShaderProperty*>(before->GetShader(other));
        if (other->name.get() == shape->name.get() && source && source->GetShaderType() == kShaderHairTint) {
          tinted->hairTintColor = source->hairTintColor;
        }
      }
    }
    const auto canonical_before = serialize_nif(*before);
    const auto canonical_after = serialize_nif(*after);
    if (!canonical_before || !canonical_after || *canonical_before != *canonical_after) {
      result.differences.push_back(
          {"", "NIF block graph", "original", "a block outside HairTint differs (material, controller, extra data or another property)"});
    }
  }
  result.unchanged_except_hair_tint = result.differences.empty();
  return result;
}

FaceGenComparison compare_facegen(std::string_view a, std::string_view b) {
  FaceGenComparison result;
  const auto na = parse_nif(std::string(a));
  const auto nb = parse_nif(std::string(b));
  if (!na || !nb) return result;
  auto shapes_b = nb->GetShapes();
  std::vector<bool> matched(shapes_b.size(), false);
  bool topology = true;
  for (auto* sa : na->GetShapes()) {
    ShapeDifference d;
    d.name = sa->name.get();
    d.in_a = true;
    d.vertices_a = sa->GetNumVertices();
    for (std::size_t j = 0; j < shapes_b.size(); ++j) {
      if (matched[j] || shapes_b[j]->name.get() != d.name) continue;
      matched[j] = true;
      d.in_b = true;
      d.vertices_b = shapes_b[j]->GetNumVertices();
      if (d.vertices_a == d.vertices_b) {
        Positions pa, pb;
        na->GetVertsForShape(sa, pa);
        nb->GetVertsForShape(shapes_b[j], pb);
        double sum = 0;
        for (std::size_t i = 0; i < pa.size(); ++i) {
          const float dist = (pa[i] - pb[i]).length();
          d.max_distance = std::max(d.max_distance, dist);
          sum += dist;
        }
        d.mean_distance = pa.empty() ? 0.0f : static_cast<float>(sum / static_cast<double>(pa.size()));
        result.max_distance = std::max(result.max_distance, d.max_distance);
      } else {
        topology = false;
      }
      break;
    }
    if (!d.in_b) topology = false;
    result.shapes.push_back(std::move(d));
  }
  for (std::size_t j = 0; j < shapes_b.size(); ++j) {
    if (matched[j]) continue;
    ShapeDifference d;
    d.name = shapes_b[j]->name.get();
    d.in_b = true;
    d.vertices_b = shapes_b[j]->GetNumVertices();
    result.shapes.push_back(std::move(d));
    topology = false;
  }
  result.same_shapes = std::all_of(result.shapes.begin(), result.shapes.end(), [](const auto& s) { return s.in_a && s.in_b; });
  result.same_topology = topology && result.same_shapes;
  return result;
}

std::vector<MorphExtension> parse_morphs_ini(std::string_view ini_text) {
  std::vector<MorphExtension> out;
  for (auto line : text::split_lines(ini_text)) {
    line = text::trim(line);
    if (line.empty() || line.front() == '#' || line.front() == ';') continue;
    const auto eq = line.find('=');
    if (eq == std::string_view::npos || !text::iequals_ascii(text::trim(line.substr(0, eq)), "extension")) continue;
    const auto value = line.substr(eq + 1);
    const auto comma = value.find(',');
    if (comma == std::string_view::npos) continue;
    const auto host = text::trim(value.substr(0, comma));
    const auto tri = text::trim(value.substr(comma + 1));
    if (host.empty() || tri.empty()) continue;
    out.push_back({std::string(host), std::string(kFaceGenMorphsRoot) + "morphs\\" + text::data_path_key(tri)});
  }
  return out;
}

std::vector<FaceIssue> audit_facegen(const FaceGenInfo& facegen, const FaceRecipe& recipe) {
  std::vector<FaceIssue> issues;
  if (!facegen.readable) {
    issue(issues, "error", "FaceGenUnreadable", "The FaceGen NIF cannot be read", recipe.facegen_mesh_path);
    return issues;
  }
  if (!facegen.has_facegen_node) {
    issue(issues, "warning", "FaceGenNodeMissing", "No BSFaceGenNiNodeSkinned node: not a FaceGen head as the game expects",
          recipe.facegen_mesh_path);
  }
  for (const auto& part : recipe.parts) {
    if (part.model.empty()) continue;
    const bool present = std::any_of(facegen.shapes.begin(), facegen.shapes.end(),
                                     [&](const auto& s) { return text::iequals_ascii(s.first, part.editor_id); });
    if (!present) {
      issue(issues, "warning", "FaceGenPartMismatch",
            "Head part '" + part.editor_id + "' of the record is not in the FaceGen file: the game shows the FaceGen "
            "head, so the record and the visible face disagree (dark-face risk)",
            part.form_key);
    }
  }
  for (const auto& [name, vertices] : facegen.shapes) {
    const bool known = std::any_of(recipe.parts.begin(), recipe.parts.end(),
                                   [&](const auto& p) { return text::iequals_ascii(p.editor_id, name); });
    if (!known) {
      issue(issues, "info", "FaceGenExtraShape",
            "FaceGen shape '" + name + "' does not match any head part of the record (custom export or older edit)", name);
    }
  }
  return issues;
}

FaceCompatibilityReport diagnose_face_compatibility(const FaceRecipe& recipe, FaceAssetCache& assets,
                                                    const std::string* winning_facegen, const FaceGenBuild& regenerated) {
  FaceCompatibilityReport r;
  auto allow = [&](std::initializer_list<const char*> ops) { r.allowed.insert(r.allowed.end(), ops.begin(), ops.end()); };
  auto block = [&](const char* op, std::string why) { r.blocked.emplace_back(op, std::move(why)); };
  auto set = [&](FaceCompatibility kind, const char* code) {
    r.kind = kind;
    r.code = code;
  };

  // Meshes and TRI files of the head parts: missing ones, and TRI files that do not fit their mesh.
  FaceGenInfo info;
  if (winning_facegen) info = inspect_facegen(*winning_facegen);
  for (const auto& part : recipe.parts) {
    if (part.model.empty()) continue;
    std::size_t mesh_vertices = 0;
    for (const auto& [name, count] : info.shapes) {
      if (text::iequals_ascii(name, part.editor_id)) mesh_vertices = count;
    }
    if (mesh_vertices == 0) {
      const auto raw = assets.bytes(part.model);
      const auto mesh = raw ? parse_nif(*raw) : nullptr;
      if (!mesh) {
        r.missing.push_back(part.editor_id + ": " + part.model);
        continue;
      }
      for (auto* s : mesh->GetShapes()) {
        if (is_drawn(*mesh, s)) {
          mesh_vertices = s->GetNumVertices();
          break;
        }
      }
    }
    for (const auto& t : part.tris) {
      std::shared_ptr<const TriFile> tri;
      try {
        tri = assets.tri(t.path);
      } catch (const std::exception&) {
        r.missing.push_back(part.editor_id + ": " + t.path + " (unreadable)");
        continue;
      }
      if (!tri) {
        r.missing.push_back(part.editor_id + ": " + t.path);
      } else if (mesh_vertices != 0 && tri->vertex_count != mesh_vertices) {
        r.tri_mismatches.push_back({part.editor_id, t.path, tri->vertex_count, mesh_vertices});
      }
    }
  }
  for (const auto& m : r.tri_mismatches) {
    r.evidence.push_back(std::format("{}: {} has {} vertices, the mesh {}", m.part, m.tri, m.tri_vertices, m.mesh_vertices));
  }
  for (const auto& m : r.missing) r.evidence.push_back("missing or unreadable: " + m);

  if (!winning_facegen) {
    set(FaceCompatibility::FaceGenMissing, "facegen-missing");
    r.evidence.push_back("no FaceGen at " + recipe.facegen_mesh_path + " in the profile: the game builds the head at run time");
    allow({"outfit", "skin-tone"});
    block("hair-color", "there is no FaceGen of the profile to recolor");
    if (regenerated.exportable()) {
      allow({"face-morphs", "head-parts", "regenerate"});
    } else {
      for (const char* op : {"face-morphs", "head-parts", "regenerate"}) block(op, "the head cannot be built from the record's head parts");
    }
    return r;
  }
  if (!info.readable) {
    set(FaceCompatibility::FaceGenInconsistent, "facegen-inconsistent");
    r.evidence.push_back("the winning FaceGen is not a readable NIF");
    allow({"outfit"});
    for (const char* op : {"hair-color", "skin-tone", "face-morphs", "head-parts", "regenerate"}) block(op, "the winning FaceGen cannot be read");
    return r;
  }
  r.hair_tint = !info.hair_tints.empty();
  r.evidence.push_back(std::format("winning FaceGen: {} shapes, {} with HairTint", info.shapes.size(), info.hair_tints.size()));
  // A replacer head may draw its own face texture in slot 6 instead of the NPC's FaceTint. A skin
  // tone or tint edit exports a generated FaceTint and points slot 6 at it: the replacer's texture
  // would be replaced by a vanilla-mask tint (Serana high poly head: 01_priscilla.dds).
  const bool custom_face_texture = !info.face_tint_slot.empty() &&
                                   text::data_path_key(info.face_tint_slot) != text::data_path_key(recipe.facegen_tint_path);
  if (custom_face_texture) r.evidence.push_back("head face texture (slot 6): " + info.face_tint_slot + ", not the NPC's FaceTint");
  const auto hair_color = [&] {
    if (r.hair_tint) {
      allow({"hair-color"});
    } else {
      block("hair-color", "no shape of the winning FaceGen uses HairTint: a CLFM color does not reach the hair");
    }
    if (custom_face_texture) {
      block("skin-tone", "the head draws its own face texture (" + info.face_tint_slot +
                             ") instead of the FaceTint: a skin tone or tint edit would replace it with a generated FaceTint");
    } else {
      allow({"skin-tone"});
    }
  };

  if (!regenerated.nif.empty()) {
    const auto cmp = compare_facegen(regenerated.nif, *winning_facegen);
    r.regeneration_deviation = cmp.same_topology ? cmp.max_distance : -1.0f;
    r.evidence.push_back(cmp.same_topology ? std::format("regeneration from the record deviates up to {:.3f} units", cmp.max_distance)
                                           : std::string("regeneration from the record has other shapes or vertex counts"));
    if (regenerated.exportable() && cmp.same_topology && cmp.max_distance < 0.05f) {
      set(FaceCompatibility::VanillaCompatible, "vanilla-compatible");
      hair_color();
      allow({"outfit", "face-morphs", "head-parts", "regenerate"});
      return r;
    }
  }
  const auto audit = audit_facegen(info, recipe);
  const bool part_mismatch =
      std::any_of(audit.begin(), audit.end(), [](const FaceIssue& i) { return i.code == "FaceGenPartMismatch" || i.code == "FaceGenNodeMissing"; });
  for (const auto& i : audit) {
    if (i.severity != "info") r.evidence.push_back(i.code + ": " + i.message);
  }
  if (part_mismatch) {
    set(FaceCompatibility::FaceGenInconsistent, "facegen-inconsistent");
    hair_color();
    allow({"outfit"});
    for (const char* op : {"face-morphs", "head-parts", "regenerate"}) block(op, "the winning FaceGen was not made from the record's head parts");
    return r;
  }
  if (!r.missing.empty()) {
    set(FaceCompatibility::MissingDependencies, "missing-dependencies");
    hair_color();
    allow({"outfit"});
    for (const char* op : {"face-morphs", "head-parts", "regenerate"}) block(op, "head part meshes or TRI files are missing");
    return r;
  }
  if (!r.tri_mismatches.empty()) {
    set(FaceCompatibility::CustomIncompatibleMorphs, "custom-incompatible-morphs");
    hair_color();
    allow({"outfit"});
    block("face-morphs", "the head's TRI files do not fit its mesh: sliders and presets cannot be applied");
    block("head-parts", "not verified for a head whose morphs do not fit");
    block("regenerate", "regenerating would lose the custom geometry and sculpt");
    return r;
  }
  if (regenerated.nif.empty()) {
    set(FaceCompatibility::Undetermined, "undetermined");
    hair_color();
    allow({"outfit"});
    for (const char* op : {"face-morphs", "head-parts", "regenerate"}) block(op, "the head could not be regenerated for comparison");
    return r;
  }
  set(FaceCompatibility::CustomLimited, "custom-limited");
  hair_color();
  allow({"outfit", "face-morphs", "head-parts"});
  block("regenerate", "regenerating would lose the custom geometry and sculpt");
  return r;
}

}  // namespace sns::face
