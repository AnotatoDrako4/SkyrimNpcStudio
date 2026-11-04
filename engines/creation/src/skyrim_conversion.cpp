#include "sns/creation/skyrim_conversion.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "sns/core/error.hpp"
#include "sns/creation/skeleton_mapping.hpp"
#include "sns/nif/mesh.hpp"

namespace sns::creation::skyrim {
namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxShapeVertices = 65535;  // BSTriShape stores uint16 vertex indices
constexpr std::size_t kMaxShapeTriangles = 65535;

struct Issues {
  Json list = Json::array();
  void add(std::string code, std::string severity, std::string object, std::string problem, std::string consequence,
           std::string solution, bool non_destructive, bool fix_available) {
    list.push_back({{"code", std::move(code)}, {"severity", std::move(severity)}, {"object", std::move(object)},
                    {"problem", std::move(problem)}, {"consequence", std::move(consequence)},
                    {"solution", std::move(solution)}, {"nonDestructive", non_destructive},
                    {"fixAvailable", fix_available}});
  }
};

std::string text(const Json& value, const char* key) {
  const auto it = value.find(key);
  return it != value.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool identity_transform(const Json& mesh) {
  for (const auto& s : mesh.value("scale", Json::array()))
    if (std::abs(s.get<double>() - 1.0) > 1e-4) return false;
  for (const auto& r : mesh.value("rotationEuler", Json::array()))
    if (std::abs(r.get<double>()) > 1e-5) return false;
  return true;
}

bool power_of_two(long long v) { return v > 0 && (v & (v - 1)) == 0; }

std::string fmt(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.1f", value);
  return buffer;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

// p = t + R (p * s)  ->  inverse: R^T, 1/s, -(R^T t) / s
nif::Transform inverse(const nif::Transform& x) {
  nif::Transform out;
  out.scale = x.scale != 0.0f ? 1.0f / x.scale : 0.0f;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) out.rotation[r][c] = x.rotation[c][r];
  const auto t = out.apply_dir(x.translation);
  out.translation = {-t.x * out.scale, -t.y * out.scale, -t.z * out.scale};
  return out;
}

std::array<std::array<double, 3>, 3> rotation_of(const nif::Transform& x) {
  std::array<std::array<double, 3>, 3> m{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) m[r][c] = x.rotation[r][c];
  return m;
}

std::array<std::array<double, 3>, 3> rotation_of(const Json& rows) {
  std::array<std::array<double, 3>, 3> m{};
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c) m[r][c] = rows.at(r).at(c).get<double>();
  return m;
}

double translation_error(const nif::Vec3& a, const Json& b) {
  return std::max({std::abs(a.x - b.at(0).get<double>()), std::abs(a.y - b.at(1).get<double>()), std::abs(a.z - b.at(2).get<double>())});
}

std::string num(double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.4g", value);
  return buffer;
}

// SkinnedMesh checks of verify_nif. The bind of bone b in the skin frame is inverse(skin_to_bone[b] after global_to_skin)
// (PyNifly export_skin: global_to_skin = skin_xf^-1, skin_to_bone = bone^-1 @ skin_xf).
template <class Check>
void verify_skin(const nif::MeshFile& mesh, const Json& prepared, const Json& report, Json& shapes, Check& check) {
  std::map<std::string, Json> blender;
  for (const auto& m : prepared.value("meshes", Json::array()))
    if (m.contains("skin")) blender[text(m, "name")] = m;
  std::set<std::string> bound;
  for (const auto& [name, m] : blender) bound.insert(text(m, "armature"));
  std::map<std::string, Json> binds, refs;
  for (const auto& a : prepared.value("armatures", Json::array()))
    if (bound.contains(text(a, "name")))
      for (const auto& b : a.value("bonesDetail", Json::array())) binds.emplace(text(b, "name"), b);
  const auto mapping = report.value("skeletonMapping", Json::object());
  for (const auto& b : mapping.value("bones", Json::array()))
    if (b.contains("referenceTranslation")) refs.emplace(text(b, "name"), b);

  bool all_skinned = true, bones_ok = true, partitions_ok = true;
  long long bad_influences = 0, bad_weights = 0, unmatched = 0, samples = 0;
  double blender_t = 0, blender_r = 0, ref_t = 0, ref_r = 0;
  bool blender_all = true, ref_all = !refs.empty();
  std::string bones_detail, partition_detail, missing_bind;
  for (std::size_t i = 0; i < mesh.shapes.size(); ++i) {
    const auto& s = mesh.shapes[i];
    const auto tris = s.indices.size() / 3;
    shapes[i]["skinned"] = s.skinned;
    shapes[i]["boneNames"] = s.bones;
    shapes[i]["partitions"] = s.partitions;
    if (s.partition_triangles) shapes[i]["partitionTriangles"] = *s.partition_triangles;
    if (!s.skinned || s.bones.empty() || s.skin_to_bone.size() != s.bones.size() || s.influences.size() != s.positions.size()) {
      all_skinned = false;
      continue;
    }
    const auto it = blender.find(s.name);
    const Json* source = it != blender.end() ? &it->second : (blender.size() == 1 && mesh.shapes.size() == 1 ? &blender.begin()->second : nullptr);
    if (!source) {
      bones_ok = false;
      bones_detail += s.name + ": sin malla de Blender correspondiente; ";
      continue;
    }
    const auto skin = source->at("skin");
    std::set<std::string> expected, got(s.bones.begin(), s.bones.end());
    const auto weighted = skin.value("weightedBones", Json::object());  // named: .items() of a temporary dangles in C++20
    for (const auto& [bone, count] : weighted.items()) expected.insert(bone);
    if (expected != got) {
      bones_ok = false;
      bones_detail += s.name + ": " + std::to_string(got.size()) + " huesos en el NIF, " + std::to_string(expected.size()) + " con pesos en Blender; ";
    }
    for (const auto& inf : s.influences) {
      int count = 0;
      double sum = 0;
      for (const auto w : inf.weight)
        if (w > 0.0f) { ++count; sum += w; }
      if (count == 0 || std::abs(sum - 1.0) > 0.01) ++bad_influences;
    }
    if (!s.partition_triangles || *s.partition_triangles != tris) {
      partitions_ok = false;
      partition_detail += s.name + ": NiSkinPartition con " + (s.partition_triangles ? std::to_string(*s.partition_triangles) : std::string("0")) +
                          " de " + std::to_string(tris) + " triángulos; ";
    }
    if (!skin.value("partitionGroups", Json::array()).empty() && s.partitions.empty()) {
      partitions_ok = false;
      partition_detail += s.name + ": los grupos SBP_* no llegaron como particiones; ";
    }
    for (std::size_t b = 0; b < s.bones.size(); ++b) {
      const auto bind = inverse(s.skin_to_bone[b].compose(s.global_to_skin));
      if (const auto found = binds.find(s.bones[b]); found != binds.end()) {
        blender_t = std::max(blender_t, translation_error(bind.translation, found->second.at("translation")));
        blender_r = std::max(blender_r, rotation_error_deg(rotation_of(bind), rotation_of(found->second.at("rotation"))));
      } else {
        blender_all = false;
        missing_bind += s.bones[b] + " ";
      }
      if (const auto found = refs.find(s.bones[b]); found != refs.end()) {
        ref_t = std::max(ref_t, translation_error(bind.translation, found->second.at("referenceTranslation")));
        ref_r = std::max(ref_r, rotation_error_deg(rotation_of(bind), rotation_of(found->second.at("referenceRotation"))));
      } else {
        ref_all = false;
      }
    }
    // Exporter-side weights of sampled Blender vertices against every NIF vertex at the same position (UV seams split
    // a Blender vertex into several NIF vertices; all of them must carry the same weights).
    for (const auto& sample : skin.value("weightSamples", Json::array())) {
      ++samples;
      const auto& p = sample.at("p");
      bool matched = false, same = true;
      for (std::size_t v = 0; v < s.positions.size(); ++v) {
        if (translation_error(s.positions[v], p) > 2e-3) continue;
        matched = true;
        std::map<std::string, double> nif_weights;
        for (std::size_t k = 0; k < 4; ++k)
          if (s.influences[v].weight[k] > 0.0f && s.influences[v].bone[k] < s.bones.size())
            nif_weights[s.bones[s.influences[v].bone[k]]] += s.influences[v].weight[k];
        for (const auto& [bone, w] : sample.at("w").items())
          if (std::abs(nif_weights[bone] - w.get<double>()) > 0.01) same = false;
        for (const auto& [bone, w] : nif_weights)
          if (!sample.at("w").contains(bone) && w > 0.01) same = false;
      }
      if (!matched) ++unmatched;
      else if (!same) ++bad_weights;
    }
  }
  check("skinned", all_skinned && !mesh.shapes.empty(),
        all_skinned ? "Todas las formas tienen skin (huesos, skin-to-bone e influencias por vértice)" : "Hay formas sin skin completo");
  check("bonesMatchWeights", bones_ok, bones_ok ? "Cada forma lleva exactamente los huesos con pesos en Blender" : bones_detail);
  check("influencesValid", bad_influences == 0,
        bad_influences == 0 ? "Cada vértice tiene de 1 a 4 influencias que suman 1"
                            : std::to_string(bad_influences) + " vértices sin influencias o que no suman 1");
  check("skinPartitions", partitions_ok, partitions_ok ? "NiSkinPartition cubre todos los triángulos; particiones SBP presentes" : partition_detail);
  const bool blender_ok = blender_all && blender_t <= kBoneTranslationTolerance && blender_r <= kBoneRotationToleranceDeg;
  check("bindMatchesBlender", blender_ok,
        (blender_all ? std::string() : "Huesos sin bind en Blender: " + missing_bind + ". ") + "Error máximo: traslación " + num(blender_t) +
            " u, rotación " + num(blender_r) + "°");
  const bool ref_ok = ref_all && ref_t <= kBoneTranslationTolerance && ref_r <= kBoneRotationToleranceDeg;
  check("bindMatchesReference", ref_ok,
        refs.empty() ? std::string("Sin esqueleto de referencia")
                     : (ref_all ? std::string() : std::string("Hay huesos del NIF ausentes de la referencia. ")) + "Error máximo frente a «" +
                           text(mapping.value("reference", Json::object()), "label") + "»: traslación " + num(ref_t) + " u, rotación " + num(ref_r) + "°");
  check("weightsMatchBlender", samples > 0 && unmatched == 0 && bad_weights == 0,
        std::to_string(samples) + " vértices muestreados: " + std::to_string(samples - unmatched - bad_weights) + " iguales, " +
            std::to_string(bad_weights) + " con pesos distintos, " + std::to_string(unmatched) + " sin vértice en el NIF");
}

}  // namespace

Json classify(const Json& report, std::string_view profile) {
  Issues out;
  const auto inspection = report.value("inspection", Json::object());
  const auto meshes = inspection.value("meshes", Json::array());
  const auto pynifly = report.value("pynifly", Json::object());

  if (profile != "static" && profile != "skinned")
    out.add("profileNotImplemented", "blocking", "", "El perfil «" + std::string(profile) + "» aún no tiene exportación validada.",
            "No se genera ningún archivo para este perfil.", "Usa el perfil «static» (recurso estático o accesorio).", true, false);
  if (text(report, "schema") != kReportSchema)
    out.add("reportSchema", "blocking", "", "El informe de Blender no usa el contrato sns.forge.skyrim/1.",
            "No se puede confiar en ningún dato del análisis.", "Actualiza tools/blender/forge_skyrim.py con la aplicación.", true, false);

  if (!pynifly.value("available", false))
    out.add("pyniflyMissing", "blocking", "", "PyNifly no está disponible en esta instalación de Blender: " +
            text(pynifly, "error") + ".", "No se puede escribir un NIF de Skyrim SE.",
            "Instala PyNifly (versión probada 29.1.0) en Blender 4/5 desde su release oficial.", true, false);
  else if (!pynifly.value("tested", false))
    out.add("pyniflyUntested", "warning", "", "La versión instalada de PyNifly no es la probada (29.1.0).",
            "El exportador puede comportarse distinto; la relectura del NIF lo comprobará.",
            "Usa PyNifly 29.1.0 para resultados reproducibles.", true, false);

  if (meshes.empty())
    out.add("noMeshes", "blocking", "", "El modelo no contiene mallas.", "No hay geometría que exportar.",
            "Importa un archivo que contenga objetos de malla.", true, false);

  const auto armatures = inspection.value("armatures", Json::array());
  for (const auto& mesh : meshes) {
    const auto name = text(mesh, "name");
    const auto vertices = mesh.value("vertices", 0LL), triangles = mesh.value("triangles", 0LL);
    const bool shape_keys = !mesh.value("shapeKeys", Json::array()).empty();
    const bool skinned = mesh.contains("armature") && mesh.at("armature").is_string();
    if (vertices == 0 || triangles == 0)
      out.add("emptyMesh", "warning", name, "La malla no tiene caras.", "Se exportaría una forma vacía.",
              "Elimina el objeto o añade geometría en Blender.", true, false);
    if (vertices > static_cast<long long>(kMaxShapeVertices) || triangles > static_cast<long long>(kMaxShapeTriangles)) {
      const auto problem = "La malla supera 65.535 vértices o triángulos (" + std::to_string(vertices) + " / " + std::to_string(triangles) + ").";
      // Same rule as forge_skyrim.py rigid(): only a mesh without morphs, rig or parent may be decimated.
      if (profile == "skinned")
        // nifly BSTriShape::Create (SSE: user 12, stream < 130) clamps both counts to 65535 without failing.
        out.add("shapeTooBig", "blocking", name, problem,
                "Skyrim SE guarda vértices e índices de BSTriShape en 16 bits; nifly truncaría la forma sin avisar.",
                "No se decima una malla con pesos o morphs. Divídela en Blender en piezas (por material o región, cada una "
                "≤ 65.535 vértices y triángulos) conservando grupos de vértices, UV y materiales.", false, false);
      else if (shape_keys || skinned || !mesh.value("parent", Json()).is_null())
        out.add("shapeTooBig", "blocking", name, problem, "BSTriShape usa índices de 16 bits: PyNifly rechaza la forma.",
                "Divide la malla en piezas o reduce polígonos (Decimate) en Blender.", false, false);
      else
        out.add("shapeTooBig", "autoFixable", name, problem, "BSTriShape usa índices de 16 bits: sin reducir, PyNifly rechaza la forma.",
                "Se reduce con Decimate a unos 60.000 triángulos en la copia de trabajo (el original no cambia); revisa la forma tras exportar.",
                true, true);
    }
    if (mesh.value("uvLayers", Json::array()).empty())
      out.add("missingUv", "blocking", name, "La malla no tiene mapa UV.", "Skyrim no puede aplicar texturas a la forma.",
              "Desenvuelve la malla en Blender (UV > Unwrap / Smart UV Project).", false, false);
    if (!identity_transform(mesh)) {
      if (shape_keys || skinned || !mesh.value("parent", Json()).is_null())
        out.add("unappliedTransforms", "manualReview", name, "Rotación o escala sin aplicar en una malla con shape keys, armature o padre.",
                "Aplicarla cambiaría morphs o pesos; no se toca automáticamente.",
                "Aplica las transformaciones en Blender comprobando morphs y rigging.", false, false);
      else
        out.add("unappliedTransforms", "autoFixable", name, "Rotación o escala sin aplicar.",
                "Un NIF solo admite escala uniforme por forma; la geometría quedaría deformada.",
                "Se aplica en la copia de trabajo antes de exportar (el original no cambia).", true, true);
    }
    if (mesh.value("negativeScale", false))
      out.add("negativeScale", "warning", name, "Escala negativa (objeto espejado).", "Las normales y caras pueden quedar invertidas.",
              "Aplica la escala y recalcula normales en Blender.", false, false);
    if (const auto n = mesh.value("nonManifoldEdges", 0LL); n > 0)
      out.add("nonManifold", "warning", name, std::to_string(n) + " aristas no manifold.", "Sombreado y colisión pueden fallar.",
              "Revisa la geometría (Select > All by Trait > Non Manifold).", false, false);
    if (const auto n = mesh.value("looseVertices", 0LL); n > 0)
      out.add("looseVertices", "warning", name, std::to_string(n) + " vértices sueltos.", "Datos inútiles en el NIF.",
              "Elimínalos en Blender (Mesh > Clean Up > Delete Loose).", false, false);
    if (const auto n = mesh.value("zeroAreaFaces", 0LL); n > 0)
      out.add("degenerateFaces", "warning", name, std::to_string(n) + " caras de área nula.", "Pueden producir normales inválidas.",
              "Mesh > Clean Up > Degenerate Dissolve en Blender.", false, false);
    if (shape_keys)
      out.add("shapeKeysDropped", "warning", name, "Tiene shape keys.", "El perfil estático no exporta morphs ni TRI.",
              "Si necesitas morphs, usa un perfil corporal cuando esté disponible.", true, false);
    if (skinned && profile == "static")
      out.add("skinnedStatic", "blocking", name, "La malla está ligada a la armature «" + text(mesh, "armature") + "».",
              "Un recurso estático no puede llevar skinning; se exportaría un NIF con huesos sin esqueleto Skyrim validado.",
              "Quita el modificador Armature para un accesorio estático o espera el perfil «armor».", false, false);
    if (profile == "skinned") {
      const auto skin = mesh.value("skin", Json::object());
      if (!skinned || skin.empty()) {
        out.add("notSkinned", "blocking", name, "La malla no está ligada a ninguna armature.",
                "El perfil SkinnedMesh solo exporta formas con skinning.", "Añade un modificador Armature con pesos o expórtala como recurso estático.",
                false, false);
      } else {
        if (const auto n = skin.value("unweightedVertices", 0LL); n > 0)
          out.add("unweightedVertices", "blocking", name, std::to_string(n) + " vértices sin peso en ningún hueso de la armature.",
                  "En el juego esos vértices no siguen a ningún hueso (se colapsan o se quedan atrás).",
                  "Asigna pesos en Weight Paint (PyNifly marca los vértices en el grupo *UNWEIGHTED*).", false, false);
        if (const auto n = skin.value("verticesOver4Influences", 0LL); n > 0)
          out.add("influencesTrimmed", "warning", name, std::to_string(n) + " vértices con más de 4 influencias.",
                  "PyNifly 29.1 conserva las 4 mayores y las normaliza (get_weights_by_bone); la relectura lo comprueba.",
                  "Limita a 4 en Blender (Weights > Limit Total) para controlar qué influencia se pierde.", true, false);
        if (const auto n = mesh.value("unnormalizedVertices", 0LL); n > 0)
          out.add("weightsNormalized", "warning", name, std::to_string(n) + " vértices cuyos pesos no suman 1.",
                  "PyNifly los normaliza al exportar; la deformación puede cambiar respecto a Blender.",
                  "Normaliza en Blender (Weights > Normalize All).", true, false);
        if (skin.value("partitionGroups", Json::array()).empty())
          out.add("noPartitions", "warning", name, "Sin grupos de partición SBP_* (p. ej. SBP_32_BODY).",
                  "PyNifly escribe las caras sin partición de desmembramiento; el slot de armadura no queda definido.",
                  "Crea el grupo de vértices de la partición (SBP_32_BODY para el cuerpo) con todas las caras.", false, false);
        if (!skin.value("nonBoneGroups", Json::array()).empty())
          out.add("nonBoneGroups", "manualReview", name, "Grupos de vértices que no son huesos: " + skin.at("nonBoneGroups").dump(),
                  "No se exportan como pesos.", "Elimínalos o renómbralos si debían ser huesos.", true, false);
      }
      if (shape_keys)
        out.add("morphsNotExported", "manualReview", name, "Tiene shape keys.", "Este incremento no escribe TRI/morphs (write_tris desactivado).",
                "Los morphs se conservan en Blender; un perfil corporal con TRI queda pendiente.", true, false);
    }
    if (mesh.value("materials", Json::array()).empty())
      out.add("noMaterial", "warning", name, "Sin material.", "Se exporta con un shader por defecto sin textura.",
              "Asigna un material con textura base en Blender.", true, false);
  }
  if (!armatures.empty() && profile == "static")
    out.add("armatureIgnored", "manualReview", text(armatures.at(0), "name"), "El modelo contiene " +
            std::to_string(armatures.size()) + " armature(s).", "El perfil estático no exporta esqueletos.",
            "Comprueba que el accesorio no dependa de huesos.", true, false);
  if (profile == "skinned") {
    std::set<std::string> bound;
    for (const auto& mesh : meshes)
      if (mesh.contains("armature") && mesh.at("armature").is_string()) bound.insert(mesh.at("armature").get<std::string>());
    if (bound.size() > 1)
      out.add("multipleArmatures", "blocking", "", "Las mallas usan " + std::to_string(bound.size()) + " armatures distintas.",
              "Un NIF skinned se valida contra un único esqueleto.", "Liga todas las mallas a la misma armature.", false, false);
    for (const auto& armature : armatures) {
      const auto name = text(armature, "name");
      if (!bound.contains(name)) continue;
      if (armature.value("bonesTruncated", false) || !armature.contains("bonesDetail"))
        out.add("rigNotInspected", "blocking", name, "No se obtuvieron todos los huesos con su transformación bind.",
                "No se puede validar el esqueleto.", "Actualiza tools/blender/forge_skyrim.py con la aplicación.", true, false);
      if (!armature.value("worldIdentity", true))
        out.add("armatureTransform", "manualReview", name, "La armature tiene posición, rotación o escala de objeto.",
                "PyNifly escribe el skin relativo a la armature; el NIF no conserva ese desplazamiento.",
                "Aplica las transformaciones de la armature y de sus mallas en Blender (Ctrl+A) y revisa el rig.", false, false);
      if (armature.value("poseDelta", 0.0) > 1e-3)
        out.add("poseNotRest", "warning", name, "La pose actual difiere de la pose de reposo.",
                "Se exporta la pose de reposo (bind); la pose no se aplica a la malla.", "Nada que hacer si la malla está modelada en reposo.",
                true, false);
    }
    const auto mapping = report.value("skeletonMapping", Json::object());
    if (mapping.empty()) {
      out.add("noReferenceSkeleton", "blocking", "", "No hay esqueleto de referencia para validar los huesos.",
              "Sin referencia no se puede afirmar que el NIF se deforme bien con el esqueleto del juego.",
              "Indica un skeleton.nif autorizado (el del juego o el de PyNifly) en solo lectura.", true, false);
    } else {
      for (const auto& bone : mapping.value("bones", Json::array())) {
        if (!bone.value("weighted", false)) continue;
        const auto status = text(bone, "status");
        const auto bone_name = text(bone, "name");
        if (status == "suggested")
          out.add("boneSuggested", "blocking", bone_name, "Hueso con pesos que solo coincide por nombre normalizado con «" + text(bone, "reference") + "».",
                  "El juego enlaza huesos por nombre exacto: no seguiría al hueso de la referencia.",
                  "Confirma la correspondencia y renombra el hueso (y su grupo de vértices) en Blender.", false, false);
        else if (status == "unrecognized")
          out.add("boneUnrecognized", "blocking", bone_name, "Hueso con pesos que no existe en el esqueleto de referencia.",
                  "Los vértices ligados a él no se moverían con el esqueleto del juego.",
                  "Transfiere sus pesos a huesos de la referencia o usa un esqueleto de criatura propio (perfil pendiente).", false, false);
        else if (status == "incompatible")
          out.add("boneIncompatible", "blocking", bone_name, "Hueso con pesos cuyo bind no coincide con la referencia: " + text(bone, "detail"),
                  "La malla se deformaría desplazada respecto al esqueleto del juego.",
                  "Ajusta el rig al esqueleto de referencia (importándolo con PyNifly) y vuelve a pesar.", false, false);
      }
      if (const auto ignored = mapping.value("counts", Json::object()).value("unrecognized", 0) -
                               mapping.value("weighted", Json::object()).value("unrecognized", 0);
          ignored > 0)
        out.add("unusedCustomBones", "manualReview", text(mapping, "armature"),
                std::to_string(ignored) + " huesos sin pesos no existen en la referencia.", "No llegan al NIF (solo se escriben huesos con pesos).",
                "Ninguna si no deben deformar nada.", true, false);
    }
  }
  for (const auto& group : inspection.value("duplicates", Json::array()))
    out.add("duplicateObjects", "warning", group.dump(), "Objetos con la misma geometría en la misma posición.",
            "Se exportarían caras duplicadas (z-fighting).", "Elimina los duplicados en Blender.", false, false);

  for (const auto& material : inspection.value("materials", Json::array())) {
    const auto name = text(material, "name");
    const auto shader = text(material, "shader");
    if (shader != "ShaderNodeBsdfPrincipled")
      out.add("unsupportedShader", "manualReview", name, "Shader «" + (shader.empty() ? std::string("ninguno") : shader) + "» no es Principled BSDF.",
              "PyNifly no encontrará las texturas por canal.", "Usa Principled BSDF con la textura en Base Color.", true, false);
    for (const auto& channel : material.value("unsupportedChannels", Json::array()))
      out.add("pbrChannelDropped", "warning", name, "Canal PBR «" + channel.get<std::string>() + "» sin equivalente directo.",
              "No se traslada al shader Bethesda; queda como metadato en el informe.",
              "Hornea el canal en la textura difusa o en el alfa del normal map si lo necesitas.", true, false);
    if (material.value("alpha", false))
      out.add("alphaReview", "manualReview", name, "El material usa transparencia.", "Debe revisarse el NiAlphaProperty resultante.",
              "Comprueba el modo de mezcla/umbral tras exportar.", true, false);
  }
  for (const auto& image : inspection.value("images", Json::array())) {
    const auto name = text(image, "name");
    const bool available = image.value("exists", false) || image.value("packed", false) || text(image, "source") == "GENERATED";
    if (!available) {
      out.add("textureMissing", "warning", name, "Textura no encontrada: " + text(image, "filepath"),
              "El canal se omite en el NIF (no se escribe una ruta absoluta ni inexistente).",
              "Localiza la imagen en Blender (File > External Data > Find Missing Files).", true, true);
      continue;
    }
    out.add("textureStaging", "autoFixable", name, "La textura no está en una carpeta Data\\textures del paquete.",
            "PyNifly escribiría una ruta absoluta del disco.", "Se copia a textures\\<prefijo>\\ y se convierte a DDS en el paquete.", true, true);
    const auto w = image.value("width", 0LL), h = image.value("height", 0LL);
    if (!power_of_two(w) || !power_of_two(h))
      out.add("textureNotPowerOfTwo", "warning", name, "Dimensiones " + std::to_string(w) + "x" + std::to_string(h) + " no son potencia de dos.",
              "Mipmaps y compresión BCn de peor calidad.", "Redimensiona a potencia de dos (p. ej. 1024 o 2048).", false, false);
    if (w > 4096 || h > 4096)
      out.add("textureTooLarge", "warning", name, "Textura mayor de 4096 px.", "Consumo de VRAM alto.", "Reduce a 2048 o 4096.", false, false);
  }

  const auto dims = inspection.value("dimensions", Json::array());
  if (dims.size() == 3 && !meshes.empty()) {
    const double extent = std::max({dims[0].get<double>(), dims[1].get<double>(), dims[2].get<double>()});
    const auto size = "La dimensión mayor mide " + fmt(extent) + " unidades (≈ " + fmt(extent * kCentimetersPerUnit) + " cm en Skyrim).";
    bool rigid = armatures.empty();
    for (const auto& mesh : meshes)
      rigid = rigid && mesh.value("shapeKeys", Json::array()).empty() && !(mesh.contains("armature") && mesh.at("armature").is_string()) &&
              mesh.value("parent", Json()).is_null();
    if (extent < 2.0 && rigid)  // forge_skyrim.py meters_to_units(): same condition
      out.add("suspiciousScale", "autoFixable", "", size + " Parece modelado en metros.",
              "Sin convertir, el recurso aparecería diminuto en el juego.",
              "Se convierte de metros a unidades de Skyrim (×70,03) en la copia de trabajo (el original no cambia).", true, true);
    else if (extent < 2.0 || extent > 20000.0)
      out.add("suspiciousScale", "warning", "", size + " Parece una escala en metros u otra unidad.",
              "El recurso aparecería diminuto o gigante en el juego.",
              "Escala el modelo en Blender (1 m ≈ 70 unidades) y aplica la escala.", false, false);
    else
      out.add("scaleCompatible", "compatible", "", size, "Exportación 1:1 sin escala adicional.", "", true, false);
  }
  if (const auto unit = inspection.value("unitScale", 1.0); std::abs(unit - 1.0) > 1e-6)
    out.add("unitScale", "warning", "", "La escena usa unit scale " + fmt(unit) + ".", "PyNifly exporta las unidades internas, no las mostradas.",
            "Comprueba las dimensiones en unidades de Blender.", true, false);

  static const std::vector<std::string> order{"blocking", "autoFixable", "warning", "manualReview", "compatible"};
  Json counts = Json::object();
  for (const auto& severity : order) counts[severity] = 0;
  for (const auto& issue : out.list) {
    auto& slot = counts[issue.at("severity").get<std::string>()];
    slot = slot.get<int>() + 1;
  }
  const auto rank = [](const Json& i) { return std::find(order.begin(), order.end(), i.at("severity").get<std::string>()) - order.begin(); };
  std::stable_sort(out.list.begin(), out.list.end(), [&](const Json& a, const Json& b) { return rank(a) < rank(b); });
  const bool exportable = counts["blocking"].get<int>() == 0;
  return {{"profile", std::string(profile)}, {"issues", out.list}, {"counts", counts}, {"exportable", exportable}};
}

std::uint32_t nif_stream_version(std::string_view bytes) {
  constexpr std::string_view header = "Gamebryo File Format, Version 20.2.0.7\n";
  if (bytes.size() < header.size() + 17 || bytes.substr(0, header.size()) != header)
    throw Error(ErrorKind::Parse, "Not a Skyrim NIF header (Gamebryo 20.2.0.7)");
  const auto u32 = [&](std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, bytes.data() + at, 4);
    return v;
  };
  std::size_t at = header.size();
  const auto version = u32(at);
  at += 4;
  const auto endian = static_cast<unsigned char>(bytes[at]);
  at += 1;
  const auto user_version = u32(at);
  at += 4 + 4;  // user version, block count
  if (version != 0x14020007u || endian != 1 || user_version != 12)
    throw Error(ErrorKind::Parse, "NIF version is not Skyrim's (20.2.0.7 / user 12)");
  return u32(at);
}

Json verify_nif(std::string_view nif_bytes, const Json& report, std::string_view profile) {
  Json checks = Json::array();
  bool passed = true;
  const auto check = [&](std::string code, bool ok, std::string detail) {
    passed = passed && ok;
    checks.push_back({{"code", std::move(code)}, {"passed", ok}, {"detail", std::move(detail)}});
  };
  const auto failed = [&](std::uint32_t stream) {
    return Json{{"passed", false}, {"checks", checks}, {"shapes", Json::array()}, {"streamVersion", stream}, {"triangles", 0}};
  };

  std::uint32_t stream = 0;
  try {
    stream = nif_stream_version(nif_bytes);
    check("formatSkyrimSE", stream == kSkyrimSeStream, "BS stream " + std::to_string(stream) + " (SE = 100, LE = 83)");
  } catch (const std::exception& e) {
    check("formatSkyrimSE", false, e.what());
    return failed(0);
  }
  nif::MeshFile mesh;
  try {
    mesh = nif::load_mesh(nif_bytes, "exported NIF");
    check("readable", !mesh.shapes.empty(), std::to_string(mesh.shapes.size()) + " forma(s) leídas con nifly");
  } catch (const std::exception& e) {
    check("readable", false, e.what());
    return failed(stream);
  }

  const auto prepared = report.contains("prepared") ? report.at("prepared") : report.value("inspection", Json::object());
  std::set<std::string> exported;
  for (const auto& name : report.value("export", Json::object()).value("objects", Json::array())) exported.insert(name.get<std::string>());
  long long expected_triangles = 0;
  std::array<double, 3> low{1e30, 1e30, 1e30}, high{-1e30, -1e30, -1e30};
  for (const auto& m : prepared.value("meshes", Json::array())) {
    if (!exported.empty() && !exported.contains(text(m, "name"))) continue;
    expected_triangles += m.value("triangles", 0LL);
    // A skinned NIF stores its vertices in the skin frame (armature space), which nifly maps back to the root.
    const auto& box = profile == "skinned" && m.contains("skin") ? m.at("skin") : m;
    for (int i = 0; i < 3; ++i) {
      low[i] = std::min(low[i], box.at("boundsMin")[i].get<double>());
      high[i] = std::max(high[i], box.at("boundsMax")[i].get<double>());
    }
  }
  std::set<std::string> staged;
  for (const auto& t : report.value("prepare", Json::object()).value("textures", Json::object()).value("staged", Json::array()))
    staged.insert(lower(text(t, "nifPath")));

  Json shapes = Json::array();
  long long triangles = 0;
  std::array<double, 3> nlow{1e30, 1e30, 1e30}, nhigh{-1e30, -1e30, -1e30};
  bool geometry = true, relative = true, packaged = true, shaders = true, unskinned = true;
  std::string geometry_detail, texture_detail, shader_detail;
  for (const auto& s : mesh.shapes) {
    const auto count = s.positions.size();
    const auto tris = s.indices.size() / 3;
    triangles += static_cast<long long>(tris);
    if (count == 0 || s.indices.size() % 3 != 0 || s.uvs.size() != count || s.normals.size() != count) {
      geometry = false;
      geometry_detail += s.name + ": " + std::to_string(count) + " vértices, " + std::to_string(s.uvs.size()) + " UV, " +
                         std::to_string(s.normals.size()) + " normales; ";
    }
    for (const auto& p : s.positions) {
      const double v[3]{p.x, p.y, p.z};
      for (int i = 0; i < 3; ++i) {
        nlow[i] = std::min(nlow[i], v[i]);
        nhigh[i] = std::max(nhigh[i], v[i]);
      }
    }
    Json textures = Json::object();
    for (std::size_t slot = 0; slot < s.textures.size(); ++slot) {
      const auto& path = s.textures[slot];
      if (path.empty()) continue;
      textures[std::to_string(slot)] = path;
      const auto lowered = lower(path);
      if (lowered.rfind("textures\\", 0) != 0 || lowered.find(':') != std::string::npos || lowered.find("..") != std::string::npos) {
        relative = false;
        texture_detail += path + " no es relativa a Data; ";
      } else if (!staged.contains(lowered)) {
        packaged = false;
        texture_detail += path + " no fue preparada por el trabajo; ";
      }
    }
    if (s.shader_block != "BSLightingShaderProperty" && s.shader_block != "BSEffectShaderProperty") {
      shaders = false;
      shader_detail += s.name + ": " + s.shader_block + "; ";
    }
    if (!s.bones.empty()) unskinned = false;
    shapes.push_back({{"name", s.name}, {"vertices", count}, {"triangles", tris}, {"uvs", s.uvs.size()},
                      {"normals", s.normals.size()}, {"bones", s.bones.size()}, {"shaderBlock", s.shader_block},
                      {"textures", textures}});
  }
  check("geometry", geometry, geometry ? "UV y normal por vértice en todas las formas" : geometry_detail);
  check("triangles", triangles == expected_triangles,
        std::to_string(triangles) + " triángulos en el NIF, " + std::to_string(expected_triangles) + " en Blender preparado");
  double tolerance = 0;
  for (int i = 0; i < 3; ++i) tolerance = std::max(tolerance, high[i] - low[i]);
  tolerance = std::max(1e-3, tolerance * 0.01);
  bool bounds = expected_triangles > 0;
  for (int i = 0; i < 3 && bounds; ++i)
    bounds = std::abs(nlow[i] - low[i]) <= tolerance && std::abs(nhigh[i] - high[i]) <= tolerance;
  check("bounds", bounds, "NIF [" + fmt(nlow[0]) + ", " + fmt(nlow[1]) + ", " + fmt(nlow[2]) + "] – [" + fmt(nhigh[0]) + ", " +
        fmt(nhigh[1]) + ", " + fmt(nhigh[2]) + "] frente a Blender [" + fmt(low[0]) + ", " + fmt(low[1]) + ", " + fmt(low[2]) +
        "] – [" + fmt(high[0]) + ", " + fmt(high[1]) + ", " + fmt(high[2]) + "]");
  check("texturePathsRelative", relative, relative ? "Todas las rutas empiezan por textures\\" : texture_detail);
  check("texturesPackaged", packaged, packaged ? "Cada textura referenciada tiene su DDS en el paquete" : texture_detail);
  check("shaderBlocks", shaders, shaders ? "Shaders Bethesda (BSLightingShaderProperty/BSEffectShaderProperty)" : shader_detail);
  if (profile == "static") check("noSkin", unskinned, unskinned ? "Sin skinning (recurso estático)" : "Hay formas con huesos");

  // Two independent nifly builds (PyNifly's DLL and the studio's) must agree on every shape.
  const auto readback = report.value("readback", Json::object()).value("shapes", Json::array());
  bool agree = readback.size() == shapes.size();
  for (std::size_t i = 0; agree && i < shapes.size(); ++i) {
    const auto it = std::find_if(readback.begin(), readback.end(), [&](const Json& r) { return text(r, "name") == shapes[i]["name"].get<std::string>(); });
    agree = it != readback.end() && it->value("triangles", -1LL) == shapes[i]["triangles"].get<long long>() &&
            it->value("vertices", -1LL) == shapes[i]["vertices"].get<long long>();
  }
  check("pyniflyAgrees", agree, agree ? "PyNifly y el nifly del estudio leen las mismas formas" : "Las dos lecturas difieren");
  if (profile == "skinned") verify_skin(mesh, prepared, report, shapes, check);
  for (const auto& w : mesh.warnings) checks.push_back({{"code", "niflyWarning"}, {"passed", true}, {"detail", w}});
  Json result{{"passed", passed}, {"checks", checks}, {"shapes", shapes}, {"streamVersion", stream}, {"triangles", triangles}};
  if (triangles > 0) {  // NIF root space, as nifly read it: what a plugin's OBND must enclose
    result["boundsMin"] = {nlow[0], nlow[1], nlow[2]};
    result["boundsMax"] = {nhigh[0], nhigh[1], nhigh[2]};
  }
  return result;
}

Json skinned_states(const Json& report, const Json& classification, const Json* verification) {
  const auto inspection = report.value("inspection", Json::object());
  bool rig = report.value("status", std::string{}) == "ok" && !inspection.value("armatures", Json::array()).empty();
  bool any_skin = false;
  for (const auto& m : inspection.value("meshes", Json::array())) any_skin = any_skin || m.contains("skin");
  rig = rig && any_skin;
  const auto mapping = report.value("skeletonMapping", Json::object());
  const bool mapped = rig && mapping.value("allWeightedVerified", false);
  const bool validated = mapped && classification.value("profile", std::string{}) == "skinned" && classification.value("exportable", false);
  const auto exported_info = report.value("export", Json::object());
  const bool exported = validated && report.value("operation", std::string{}) == "exportNif" && exported_info.contains("sha256") &&
                        report.contains("readback");
  const bool verified = exported && verification != nullptr && verification->value("passed", false);
  const auto weighted = mapping.value("weighted", Json::object());
  const std::vector<std::pair<std::string, std::pair<bool, std::string>>> chain{
      {"PREVIEW_ONLY", {true, "Solo vista previa: ningún archivo de Skyrim validado."}},
      {"RIG_INSPECTED", {rig, rig ? "Armature y mallas con pesos inspeccionadas." : "No hay malla ligada a una armature inspeccionada."}},
      {"SKELETON_MAPPED", {mapped, mapping.empty() ? "Sin esqueleto de referencia."
                                   : "Huesos con pesos: " + std::to_string(weighted.value("verified", 0)) + " verificados, " +
                                         std::to_string(weighted.value("suggested", 0)) + " sugeridos, " +
                                         std::to_string(weighted.value("unrecognized", 0)) + " no reconocidos, " +
                                         std::to_string(weighted.value("incompatible", 0)) + " incompatibles."}},
      {"SKIN_VALIDATED", {validated, validated ? "Sin problemas bloqueantes en el perfil SkinnedMesh."
                                               : std::to_string(classification.value("counts", Json::object()).value("blocking", 0)) + " bloqueantes."}},
      {"NIF_EXPORTED", {exported, exported ? "PyNifly escribió el NIF y lo releyó." : "NIF no exportado."}},
      {"NIF_VERIFIED", {verified, verified ? "Relectura con el nifly del estudio: todas las comprobaciones superadas."
                                           : "NIF no verificado por el estudio."}},
      {"PENDIENTE_SKYRIM", {verified, "La carga en el juego requiere observación humana (VERIFICADO_EN_SKYRIM nunca se infiere)."}}};
  Json states = Json::array();
  std::string current = "PREVIEW_ONLY";
  bool chain_ok = true;
  for (const auto& [state, value] : chain) {
    chain_ok = chain_ok && value.first;
    states.push_back({{"state", state}, {"reached", chain_ok}, {"detail", value.second}});
    if (chain_ok) current = state;
  }
  return {{"current", current}, {"states", states}};
}

}  // namespace sns::creation::skyrim
