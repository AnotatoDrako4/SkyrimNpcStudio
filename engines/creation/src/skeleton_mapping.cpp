#include "sns/creation/skeleton_mapping.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include "sns/nif/mesh.hpp"

namespace sns::creation::skyrim {
namespace {

using Json = nlohmann::json;
using Matrix3 = std::array<std::array<double, 3>, 3>;

Matrix3 matrix(const Json& rows) {
  Matrix3 m{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) m[r][c] = rows.at(r).at(c).get<double>();
  return m;
}

std::string text(const Json& value, const char* key) {
  const auto it = value.find(key);
  return it != value.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

}  // namespace

ReferenceSkeleton load_reference_skeleton(std::string_view nif_bytes, std::string label, std::string sha256) {
  const auto skeleton = nif::load_skeleton(nif_bytes, label);
  ReferenceSkeleton out{std::move(label), std::move(sha256), {}};
  out.bones.reserve(skeleton.bones.size());
  for (const auto& bone : skeleton.bones) {
    ReferenceBone b;
    b.name = bone.name;
    if (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) < skeleton.bones.size())
      b.parent = skeleton.bones[static_cast<std::size_t>(bone.parent)].name;
    b.translation = {bone.to_global.translation.x, bone.to_global.translation.y, bone.to_global.translation.z};
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c) b.rotation[r][c] = bone.to_global.rotation[r][c];
    b.scale = bone.to_global.scale;
    out.bones.push_back(std::move(b));
  }
  return out;
}

std::string normalized_bone_name(std::string_view name) {
  std::string out;
  int depth = 0;
  for (const char raw : name) {
    const auto c = static_cast<unsigned char>(raw);
    if (c == '[') { ++depth; continue; }
    if (c == ']') { depth = std::max(0, depth - 1); continue; }
    if (depth > 0) continue;
    const char v = (c == '_' || c == '.' || c == '-' || std::isspace(c)) ? ' ' : static_cast<char>(std::tolower(c));
    if (v == ' ' && (out.empty() || out.back() == ' ')) continue;
    out.push_back(v);
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

double rotation_error_deg(const Matrix3& a, const Matrix3& b) {
  // trace(A^T B) = 1 + 2 cos(theta)
  double trace = 0;
  for (int i = 0; i < 3; ++i)
    for (int k = 0; k < 3; ++k) trace += a[k][i] * b[k][i];
  const double c = std::clamp((trace - 1.0) / 2.0, -1.0, 1.0);
  return std::acos(c) * 180.0 / 3.14159265358979323846;
}

Json map_skeleton(const Json& armature, const ReferenceSkeleton& reference, const std::vector<std::string>& weighted_bones) {
  std::map<std::string, const ReferenceBone*> exact;
  std::map<std::string, std::vector<const ReferenceBone*>> by_normalized;
  for (const auto& bone : reference.bones) {
    exact.emplace(bone.name, &bone);
    by_normalized[normalized_bone_name(bone.name)].push_back(&bone);
  }
  const std::set<std::string> weighted(weighted_bones.begin(), weighted_bones.end());
  const auto details = armature.value("bonesDetail", Json::array());
  std::set<std::string> in_armature;
  for (const auto& b : details) in_armature.insert(text(b, "name"));

  Json bones = Json::array();
  Json counts{{"verified", 0}, {"suggested", 0}, {"unrecognized", 0}, {"incompatible", 0}};
  Json weighted_counts = counts;
  bool all_weighted_verified = true;
  std::set<std::string> seen_weighted;
  for (const auto& b : details) {
    const auto name = text(b, "name");
    const auto parent = text(b, "parent");
    Json entry{{"name", name}, {"weighted", weighted.contains(name)}, {"parent", parent}};
    const ReferenceBone* ref = nullptr;
    std::string status;
    if (const auto it = exact.find(name); it != exact.end()) {
      ref = it->second;
    } else if (const auto norm = by_normalized.find(normalized_bone_name(name)); norm != by_normalized.end() && norm->second.size() == 1) {
      ref = norm->second.front();
      status = "suggested";
    } else {
      status = "unrecognized";
      entry["detail"] = "El esqueleto de referencia no tiene ningún hueso con este nombre.";
    }
    if (ref) {
      entry["reference"] = ref->name;
      entry["referenceParent"] = ref->parent;
      entry["referenceTranslation"] = ref->translation;
      entry["referenceRotation"] = ref->rotation;
      double translation = 0;
      const auto& t = b.at("translation");
      for (int i = 0; i < 3; ++i) translation = std::max(translation, std::abs(t.at(i).get<double>() - ref->translation[i]));
      const double rotation = rotation_error_deg(matrix(b.at("rotation")), ref->rotation);
      const double scale = std::abs(b.value("scale", 1.0) - ref->scale);
      entry["translationError"] = translation;
      entry["rotationErrorDeg"] = rotation;
      // A parent that is not part of this armature (partial rig) cannot contradict the reference.
      const bool parent_ok = parent == ref->parent || (parent.empty() && !in_armature.contains(ref->parent));
      const bool bind_ok = translation <= kBoneTranslationTolerance && rotation <= kBoneRotationToleranceDeg && scale <= 1e-4;
      if (status.empty()) {
        if (parent_ok && bind_ok) {
          status = "verified";
        } else {
          status = "incompatible";
          std::string why;
          if (!parent_ok) why += "Padre «" + parent + "» frente a «" + ref->parent + "» en la referencia. ";
          if (!bind_ok)
            why += "Bind distinto de la referencia (traslación " + std::to_string(translation) + " u, rotación " +
                   std::to_string(rotation) + "°, escala " + std::to_string(scale) + ").";
          entry["detail"] = why;
        }
      } else {
        entry["detail"] = "Solo coincide el nombre normalizado con «" + ref->name +
                          "»: requiere confirmación humana y renombrar el hueso en Blender; nunca se aplica sola.";
      }
    }
    entry["status"] = status;
    counts[status] = counts[status].get<int>() + 1;
    if (weighted.contains(name)) {
      seen_weighted.insert(name);
      weighted_counts[status] = weighted_counts[status].get<int>() + 1;
      all_weighted_verified = all_weighted_verified && status == "verified";
    }
    bones.push_back(std::move(entry));
  }
  // A weighted group whose bone is missing from the reported armature (truncated report) is never verified.
  for (const auto& name : weighted)
    if (!seen_weighted.contains(name)) {
      all_weighted_verified = false;
      weighted_counts["unrecognized"] = weighted_counts["unrecognized"].get<int>() + 1;
      bones.push_back({{"name", name}, {"weighted", true}, {"status", "unrecognized"},
                       {"detail", "El hueso con pesos no figura en el informe de la armature."}});
    }
  return {{"reference", {{"label", reference.label}, {"sha256", reference.sha256}, {"bones", reference.bones.size()}}},
          {"armature", text(armature, "name")}, {"bones", bones}, {"counts", counts}, {"weighted", weighted_counts},
          {"allWeightedVerified", all_weighted_verified && !weighted.empty()},
          {"tolerances", {{"translation", kBoneTranslationTolerance}, {"rotationDeg", kBoneRotationToleranceDeg}}}};
}

}  // namespace sns::creation::skyrim
