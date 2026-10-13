#include "sns/face/facegen.hpp"

#include <NifFile.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

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

HairTintPatch patch_hair_tint(std::string_view facegen_nif, const FaceRecipe& recipe) {
  HairTintPatch patch;
  const auto nif = parse_nif(std::string(facegen_nif));
  if (!nif) {
    issue(patch.issues, "error", "FaceGenUnreadable", "The FaceGen to update is not a readable NIF", recipe.facegen_mesh_path);
    return patch;
  }
  const auto tint = hair_tint_vector(recipe.values.hair_color);
  for (auto* shape : nif->GetShapes()) {
    auto* shader = nif->GetShader(shape);
    if (auto* lighting = dynamic_cast<nifly::BSLightingShaderProperty*>(shader);
        lighting && lighting->GetShaderType() == kShaderHairTint) {
      lighting->hairTintColor = tint;
      patch.patched.push_back(shape->name.get());
      continue;
    }
    const auto part = std::find_if(recipe.parts.begin(), recipe.parts.end(),
                                   [&](const HeadPartData& p) { return text::iequals_ascii(p.editor_id, shape->name.get()); });
    if (part != recipe.parts.end() && is_hair_colored(part->type)) hair_tint_unsupported(patch.issues, *part, shader);
  }
  std::ostringstream stream(std::ios::out | std::ios::binary);
  if (nif->Save(stream) != 0) {
    issue(patch.issues, "error", "FaceGenWriteFailed", "nifly could not serialize the FaceGen NIF", recipe.npc_form_key);
    return patch;
  }
  patch.nif = std::move(stream).str();
  return patch;
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

}  // namespace sns::face
