#include "sns/creation/forge_easy_mode.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace sns::creation::easy {
namespace {

using Json = nlohmann::json;

std::string str(const Json& value, const char* key) {
  if (!value.is_object()) return {};
  const auto it = value.find(key);
  return it != value.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool flag(const Json& value, const char* key) {
  if (!value.is_object()) return false;
  const auto it = value.find(key);
  return it != value.end() && it->is_boolean() && it->get<bool>();
}

const Json& issues_of(const Json& classification) {
  static const Json empty = Json::array();
  if (!classification.is_object()) return empty;
  const auto it = classification.find("issues");
  return it != classification.end() && it->is_array() ? *it : empty;
}

bool has_issue(const Json& classification, std::string_view code) {
  const auto& list = issues_of(classification);
  return std::any_of(list.begin(), list.end(), [&](const Json& i) { return str(i, "code") == code; });
}

int count_state(const Json& classification, std::string_view state) {
  const auto& list = issues_of(classification);
  return static_cast<int>(std::count_if(list.begin(), list.end(), [&](const Json& i) { return ui_state(i) == state; }));
}

// Worst first: what the summary of a diagnosis reports.
constexpr std::array<std::string_view, 5> kOrder{kBlocking, kManualReview, kSafeFix, kNotVerified, kCompatible};

std::size_t rank(std::string_view state) {
  return static_cast<std::size_t>(std::find(kOrder.begin(), kOrder.end(), state) - kOrder.begin());
}

const char* kNeedIndexed = "Perfil de MO2 cargado e indexado por el BethesdaWorker";
const char* kNextIndexed = "Carga tu instancia de MO2 y un perfil en la pantalla de inicio; espera a que termine la indexación.";
const char* kNeedBlender = "Blender 5.x con PyNifly detectado";
const char* kNextBlender = "En «Blender y MCP» pulsa «Detectar Blender» o elige blender.exe.";

Json action(const char* id, const char* title, const char* description, const char* target) {
  return {{"id", id}, {"title", title}, {"description", description}, {"state", "available"}, {"reason", ""},
          {"missing", Json::array()}, {"next", ""}, {"target", target}};
}

void block(Json& a, std::string reason, std::string next) {
  a["state"] = "blocked";
  a["reason"] = std::move(reason);
  a["next"] = std::move(next);
}

void partial(Json& a, std::string reason, std::string next = {}) {
  a["state"] = "partial";
  a["reason"] = std::move(reason);
  if (!next.empty()) a["next"] = std::move(next);
}

bool load_order_ready(const Facts& f) { return f.worker_ready && f.profile_indexed; }

std::string load_order_reason(const Facts& f) {
  if (!f.worker_ready)
    return f.worker_message.empty() ? std::string("El BethesdaWorker no está listo.")
                                    : "El BethesdaWorker no está listo: " + f.worker_message;
  return "El perfil de MO2 no está indexado: sin él no se leen NPCs, cuerpos ni prendas.";
}

bool needs_skinned(const Facts& f) { return f.profile == "skinned" || has_issue(f.classification, "skinnedStatic"); }

// The diagnosis exists once the analysis ended; exportNif only runs (or fails) after it.
bool analysis_done(const Facts& f) {
  return f.conversion_state == "analyzed" || f.conversion_state == "exported" || f.conversion_state == "verificationFailed" ||
         ((f.conversion_state == "running" || f.conversion_state == "error") && f.conversion_operation == "exportNif");
}

Json step(const char* id, const char* title) { return {{"id", id}, {"title", title}, {"state", "pending"}, {"detail", ""}}; }

}  // namespace

std::string_view ui_state(const Json& issue) {
  const auto severity = str(issue, "severity");
  if (severity == "blocking") return kBlocking;
  if (severity == "autoFixable") return flag(issue, "fixAvailable") ? kSafeFix : kManualReview;
  if (severity == "warning" || severity == "manualReview") return kManualReview;
  if (severity == "compatible") return kCompatible;
  return kNotVerified;
}

Json explain(const Json& classification) {
  Json findings = Json::array();
  Json counts = Json::object();
  for (const auto s : kOrder) counts[std::string(s)] = 0;
  const auto& list = issues_of(classification);
  std::string worst(kCompatible);
  for (const auto& issue : list) {
    const auto state = std::string(ui_state(issue));
    counts[state] = counts[state].get<int>() + 1;
    if (rank(state) < rank(worst)) worst = state;
    const auto object = str(issue, "object");
    findings.push_back({{"code", str(issue, "code")},
                        {"uiState", state},
                        {"severity", str(issue, "severity")},
                        {"resource", object.empty() ? std::string("Escena completa") : object},
                        {"problem", str(issue, "problem")},
                        {"whySkyrim", str(issue, "consequence")},
                        {"operation", str(issue, "solution")},
                        // Repairs run on the working copy of the job: the user's file is never written.
                        {"reversible", flag(issue, "nonDestructive")},
                        {"requiresUser", state == kBlocking || state == kManualReview},
                        {"blocksExport", state == kBlocking},
                        {"pending", false}});
  }
  if (!classification.is_object() || !classification.contains("issues")) {
    return {{"state", std::string(kNotVerified)}, {"summary", "Aún no hay diagnóstico: analiza el modelo."},
            {"counts", counts}, {"findings", findings}, {"exportable", false}};
  }
  // What no validator of the studio can check: always listed, never counted in the state.
  findings.push_back({{"code", "inGame"}, {"uiState", std::string(kNotVerified)}, {"severity", "pending"},
                      {"resource", "Mod completo"}, {"problem", "La carga dentro de Skyrim no se ha comprobado."},
                      {"whySkyrim", "Solo una prueba en el juego confirma escala, materiales e iluminación."},
                      {"operation", "Instala el ZIP en MO2 en un perfil de prueba y coloca el objeto (player.placeatme)."},
                      {"reversible", true}, {"requiresUser", true}, {"blocksExport", false}, {"pending", true}});
  if (str(classification, "profile") != "skinned") {
    findings.push_back({{"code", "collision"}, {"uiState", std::string(kNotVerified)}, {"severity", "pending"},
                        {"resource", "NIF"}, {"problem", "El NIF no lleva colisión (bhkCollisionObject)."},
                        {"whySkyrim", "El jugador y los objetos atraviesan el recurso."},
                        {"operation", "Añade colisión en una herramienta dedicada si el recurso debe ser sólido."},
                        {"reversible", true}, {"requiresUser", true}, {"blocksExport", false}, {"pending", true}});
  }
  const auto exportable = flag(classification, "exportable");
  std::string summary;
  if (worst == kBlocking)
    summary = std::to_string(counts[std::string(kBlocking)].get<int>()) + " problema(s) bloqueante(s): no se puede exportar.";
  else if (worst == kManualReview)
    summary = "Exportable, con " + std::to_string(counts[std::string(kManualReview)].get<int>()) + " punto(s) para revisar.";
  else if (worst == kSafeFix)
    summary = "Exportable: " + std::to_string(counts[std::string(kSafeFix)].get<int>()) +
              " corrección(es) segura(s) se aplicarán sobre una copia.";
  else
    summary = "Compatible: nada que corregir.";
  return {{"state", worst}, {"summary", summary}, {"counts", counts}, {"findings", findings}, {"exportable", exportable}};
}

Json actions(const Facts& f) {
  Json out = Json::array();

  auto create = action("createCharacter", "Crear o importar un personaje",
                       "Proyecto .sncf con el registro NPC: raza, nombre, estadísticas, inventario.", "forge.npc");
  if (!load_order_ready(f)) {
    create["missing"].push_back(kNeedIndexed);
    partial(create, "Puedes crear el proyecto y editar el registro; validar y exportar necesitan el perfil indexado. "
                    "El rostro (FaceGen) no se genera: el perfil npc está BLOQUEADO.",
            kNextIndexed);
  } else {
    partial(create, "Registro NPC editable y exportable como parche técnico. El rostro (FaceGen) no se genera: "
                    "el perfil npc está BLOQUEADO.");
  }
  out.push_back(std::move(create));

  auto npc = action("editNpc", "Modificar un NPC existente",
                    "Rostro, pelo, altura y atuendo de un NPC del perfil, exportado como Safe Patch.", "creator");
  if (!load_order_ready(f)) {
    npc["missing"].push_back(kNeedIndexed);
    block(npc, load_order_reason(f), kNextIndexed);
  } else if (!f.npc_loaded) {
    npc["reason"] = "Elige un NPC en el buscador del Creador de personajes.";
  }
  out.push_back(std::move(npc));

  auto body = action("bodyAppearance", "Cambiar cuerpo o apariencia",
                     "Cuerpo resuelto, familia (CBBE/3BA, UBE), presets BodySlide y morphs del NPC.", "creator.body");
  if (!load_order_ready(f)) {
    body["missing"].push_back(kNeedIndexed);
    block(body, load_order_reason(f), kNextIndexed);
  } else {
    partial(body, "Diagnóstico del cuerpo y presets BodySlide disponibles. CBBE/3BA y UBE no son intercambiables: "
                  "la conversión entre familias no está certificada y no se ofrece.");
  }
  out.push_back(std::move(body));

  auto outfit = action("outfits", "Adaptar ropa o armaduras",
                       "Prendas compatibles, conflictos de slots y archivos, dependencias que faltan.", "outfit");
  if (!load_order_ready(f)) {
    outfit["missing"].push_back(kNeedIndexed);
    block(outfit, load_order_reason(f), kNextIndexed);
  } else {
    partial(outfit, "Diagnóstico de ajuste y selección de prendas disponibles (Outfit Fit Doctor). Convertir una prenda "
                    "a otro cuerpo (perfil outfit) no está implementado.");
  }
  out.push_back(std::move(outfit));

  auto import = action("importBlender", "Importar un modelo de Blender",
                       "BLEND, FBX, OBJ, GLB o glTF: vista previa 3D sin tocar el archivo original.", "forge.import");
  if (!f.blender_found) {
    import["missing"].push_back(kNeedBlender);
    partial(import, "Sin Blender solo se previsualizan GLB y glTF; BLEND, FBX y OBJ se convierten con Blender.", kNextBlender);
  }
  out.push_back(std::move(import));

  auto prepare = action("prepareResource", "Preparar un recurso para Skyrim",
                        "Diagnóstico, correcciones seguras sobre una copia, NIF y DDS verificados.", "forge.skyrim");
  if (!f.model_selected) prepare["missing"].push_back("Un modelo importado");
  if (!f.blender_found) prepare["missing"].push_back(kNeedBlender);
  if (!f.model_selected)
    block(prepare, "No hay ningún modelo seleccionado.", "Usa «Importar un modelo de Blender».");
  else if (!f.blender_found)
    block(prepare, "El análisis y la exportación NIF se hacen en Blender con PyNifly.", kNextBlender);
  else if (needs_skinned(f))
    partial(prepare, "El modelo tiene armature o pesos: se prepara con el perfil skinned (NIF con skin verificado). "
                     "No es un objeto estático (STAT).");
  out.push_back(std::move(prepare));

  auto mod = action("exportMod", "Exportar un mod para MO2",
                    "ESP-ESL con un STAT, NIF y texturas en un ZIP nuevo, verificado y aislado.", "forge.skyrim");
  const bool exported = f.conversion_state == "exported";
  if (needs_skinned(f)) {
    block(mod, "El perfil skinned no crea registros ARMO/ARMA todavía: un modelo con skin no se exporta como STAT.",
          "Prepara el NIF skinned para revisarlo; el mod de prenda llegará con el perfil outfit.");
  } else if (f.mod_state == "done") {
    mod["reason"] = "Mod creado y verificado. La prueba dentro de Skyrim sigue pendiente (PENDIENTE_SKYRIM).";
  } else {
    if (!(exported && f.nif_verified)) mod["missing"].push_back("NIF exportado y verificado con nifly");
    if (!f.worker_ready) mod["missing"].push_back("BethesdaWorker listo");
    if (!f.profile_indexed) mod["missing"].push_back(kNeedIndexed);
    if (count_state(f.classification, kBlocking) > 0) {
      block(mod, "El diagnóstico tiene " + std::to_string(count_state(f.classification, kBlocking)) +
                     " problema(s) bloqueante(s).", "Corrige el modelo en Blender y vuelve a analizarlo.");
    } else if (f.conversion_state == "verificationFailed") {
      block(mod, "La relectura del NIF con nifly no superó todas las comprobaciones.", "Revisa la verificación en el detalle técnico.");
    } else if (!(exported && f.nif_verified)) {
      block(mod, "Primero hay que preparar el recurso: el mod se crea a partir del NIF verificado.", "Usa «Preparar un recurso para Skyrim».");
    } else if (!load_order_ready(f)) {
      block(mod, load_order_reason(f), kNextIndexed);
    } else if (f.mod_state == "error") {
      mod["reason"] = "El último intento falló: " + f.mod_message;
    }
  }
  out.push_back(std::move(mod));
  return out;
}

Json flow(const Facts& f) {
  auto select = step("select", "Seleccionar");
  auto preview = step("preview", "Previsualizar");
  auto diagnose = step("diagnose", "Diagnosticar");
  auto prepare = step("prepare", "Preparar");
  auto validate = step("validate", "Validar");
  auto exp = step("export", "Exportar");

  const auto set = [](Json& s, const char* state, std::string detail) {
    s["state"] = state;
    s["detail"] = std::move(detail);
  };

  // SELECCIONAR
  if (!f.model_selected) set(select, "current", "Elige un archivo BLEND, FBX, OBJ, GLB o glTF.");
  else set(select, "done", "Modelo " + (f.model_format.empty() ? std::string("seleccionado") : f.model_format) + ".");

  // PREVISUALIZAR (a failed preview does not stop the diagnosis: Blender reads the source file, not the preview GLB)
  if (f.model_selected) {
    if (f.preview_loaded) set(preview, "done", "Vista previa cargada (GLB ligero, no es la fuente del NIF).");
    else if (f.preview_state == "loading" || (f.busy && f.conversion_state == "none")) set(preview, "running", "Cargando la vista previa…");
    else if (f.preview_state == "error") set(preview, "blocked", "La vista previa falló; el diagnóstico sigue disponible.");
    else if ((f.model_format == "blend" || f.model_format == "fbx" || f.model_format == "obj") && !f.blender_found)
      set(preview, "blocked", "Este formato se convierte a GLB con Blender, que no está detectado.");
    else set(preview, "current", "Genera la vista previa del modelo.");
  }

  // DIAGNOSTICAR
  const bool running_inspect = f.conversion_state == "running" && f.conversion_operation != "exportNif";
  const auto blocking = count_state(f.classification, kBlocking);
  if (f.model_selected) {
    if (running_inspect) set(diagnose, "running", "Analizando el modelo en Blender…");
    else if (analysis_done(f)) {
      if (blocking > 0) set(diagnose, "blocked", std::to_string(blocking) + " problema(s) bloqueante(s): ver el diagnóstico.");
      else set(diagnose, "done", explain(f.classification).value("summary", std::string{}));
    } else if (f.conversion_state == "error" && f.conversion_operation != "exportNif") {
      set(diagnose, "blocked", "El análisis falló: revisa el detalle técnico.");
    } else if (!f.blender_found) {
      set(diagnose, "blocked", "El análisis se hace en Blender, que no está detectado.");
    } else if (preview.at("state") == "current" || preview.at("state") == "running") {
      // One step at a time: the diagnosis waits for the preview (a failed preview does not hold it, see above)
      set(diagnose, "pending", "Tras la vista previa.");
    } else {
      set(diagnose, "current", "Analiza la compatibilidad con Skyrim.");
    }
  }

  // PREPARAR
  if (diagnose.at("state") == "done") {
    const auto fixes = count_state(f.classification, kSafeFix);
    if (f.conversion_state == "running") set(prepare, "running", "Aplicando correcciones seguras y exportando el NIF…");
    else if (f.conversion_state == "exported" || f.conversion_state == "verificationFailed")
      set(prepare, "done", "Copia preparada y NIF exportado; el original no se modificó.");
    else if (f.conversion_state == "error") set(prepare, "blocked", "La exportación falló: revisa el detalle técnico.");
    else set(prepare, "current", fixes > 0 ? std::to_string(fixes) + " corrección(es) segura(s) sobre una copia, luego NIF y DDS."
                                           : "Exporta el NIF y las texturas DDS.");
  }

  // VALIDAR
  if (prepare.at("state") == "done") {
    if (f.conversion_state == "exported" && f.nif_verified) set(validate, "done", "NIF releído con nifly: todas las comprobaciones pasan.");
    else set(validate, "blocked", "La relectura del NIF con nifly falló: ver el detalle técnico.");
  }

  // EXPORTAR
  if (validate.at("state") == "done") {
    if (needs_skinned(f)) set(exp, "blocked", "El perfil skinned no crea el mod (sin ARMO/ARMA todavía).");
    else if (f.mod_state == "done") set(exp, "done", "Mod MO2 creado y verificado. Prueba en Skyrim: PENDIENTE_SKYRIM.");
    else if (f.mod_state == "running") set(exp, "running", "Creando el ESP-ESL y el ZIP con el BethesdaWorker…");
    else if (!load_order_ready(f)) set(exp, "blocked", load_order_reason(f));
    else if (f.mod_state == "error") set(exp, "current", "El último intento falló: " + f.mod_message);
    else set(exp, "current", "Crea el mod MO2 (ESP-ESL + ZIP).");
  }

  Json steps = Json::array({select, preview, diagnose, prepare, validate, exp});
  std::string current;
  for (const auto& s : steps) {
    const auto state = s.at("state").get<std::string>();
    // A failed preview is reported but does not hold the flow.
    if (s.at("id") == "preview" && state == "blocked") continue;
    if (state == "current" || state == "running" || state == "blocked") {
      current = s.at("id").get<std::string>();
      break;
    }
  }
  const bool can_export = exp.at("state") == "current" && !f.busy;
  return {{"steps", steps}, {"current", current}, {"canExport", can_export}};
}

namespace {

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

const Json& arr(const Json& value, const char* key) {
  static const Json empty = Json::array();
  if (!value.is_object()) return empty;
  const auto it = value.find(key);
  return it != value.end() && it->is_array() ? *it : empty;
}

const Json& obj(const Json& value, const char* key) {
  static const Json empty = Json::object();
  if (!value.is_object()) return empty;
  const auto it = value.find(key);
  return it != value.end() && it->is_object() ? *it : empty;
}

int num(const Json& value, const char* key) {
  if (!value.is_object()) return 0;
  const auto it = value.find(key);
  return it != value.end() && it->is_number() ? it->get<int>() : 0;
}

// body::Issue and the worker's Issue: "error" blocks, "warning" asks for review, "info" is a note.
std::string_view state_of_severity(const std::string& severity) {
  const auto s = lower(severity);
  if (s == "error") return kBlocking;
  if (s == "warning") return kManualReview;
  return kCompatible;
}

bool is_missing_code(const std::string& code) {
  return code == "OutfitMeshMissing" || code == "OutfitMeshUnreadable" || code == "EmptyMeshPath" || code == "AddonNotFound" ||
         code == "ArmorNotFound" || code == "ArmorNotIndexed" || code == "NpcNotFound";
}

// "CBBE 3BA", "cbbe3ba", "CBBE-3BA" -> "cbbe3ba"
std::string family_key(const std::string& family) {
  std::string out;
  for (const char c : lower(family))
    if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(c);
  return out;
}

bool known_family(const std::string& family) {
  const auto f = lower(family);
  return !f.empty() && f != "unknown" && f != "other";
}

}  // namespace

Json npc_overview(const Json& in) {
  const auto& npc = obj(in, "npc");
  const auto npc_state = str(npc, "state");
  const auto form_key = str(npc, "formKey");
  Json out{{"state", "none"}, {"message", ""}, {"npc", {{"formKey", form_key}, {"name", str(npc, "name")}}},
           {"body", Json::object()}, {"presets", Json::object()}, {"familyChanges", Json::array()},
           {"outfits", {{"verified", false}, {"compatible", Json::array()}, {"needsConversion", Json::array()}, {"unverified", Json::array()}}},
           {"conflicts", Json::array()}, {"missing", Json::array()}, {"findings", Json::array()}, {"exports", Json::array()}};
  if (!flag(in, "indexed")) {
    out["message"] = "Carga e indexa un perfil de MO2 para ver un NPC.";
    return out;
  }
  if (form_key.empty() || npc_state.empty() || npc_state == "none") {
    out["message"] = "Elige un NPC en el Creador de personajes.";
    return out;
  }
  if (npc_state == "error") {
    out["state"] = "error";
    out["message"] = "El análisis del cuerpo falló: " + str(npc, "error");
    return out;
  }
  if (npc_state != "ready") {
    out["state"] = "loading";
    out["message"] = "Analizando el cuerpo del NPC…";
    return out;
  }
  out["state"] = "ready";

  auto& findings = out["findings"];
  auto& missing = out["missing"];
  auto& conflicts = out["conflicts"];

  // ---- Body: what the BodyDirector resolved (winning body NIF, family, TRI)
  const auto& body = obj(npc, "body");
  std::string body_state(kCompatible);
  for (const auto& issue : arr(npc, "issues")) {
    const auto state = std::string(state_of_severity(str(issue, "severity")));
    if (rank(state) < rank(body_state)) body_state = state;
    const auto code = str(issue, "code");
    findings.push_back({{"code", code}, {"uiState", state}, {"resource", "Cuerpo"}, {"problem", str(issue, "message")},
                        {"operation", ""}, {"blocksExport", state == kBlocking}});
    if (is_missing_code(code)) missing.push_back(str(issue, "message"));
  }
  if (!known_family(str(npc, "family"))) body_state = std::string(kNotVerified);
  out["body"] = {{"family", str(npc, "family")}, {"familyLabel", str(npc, "familyLabel")}, {"provider", str(body, "provider")},
                 {"path", str(body, "path")}, {"tri", str(body, "tri")}, {"uiState", body_state},
                 {"detail", known_family(str(npc, "family")) ? "Familia detectada por el cuerpo que gana en el perfil."
                                                             : "Familia del cuerpo sin determinar: no se ofrecen presets ni prendas por familia."}};

  // ---- Presets: verdicts the BodyDirector gave for this NPC
  Json presets{{"total", 0}, {"compatible", 0}, {"experimental", 0}, {"needsResources", 0}, {"blocked", 0}};
  for (const auto& p : in.value("presets", Json::array())) {
    const auto v = str(p, "verdict");
    if (v.empty()) continue;
    presets["total"] = presets["total"].get<int>() + 1;
    const char* key = v == "compatible" ? "compatible" : v == "experimental" ? "experimental" : v == "needs-resources" ? "needsResources" : "blocked";
    presets[key] = presets[key].get<int>() + 1;
  }
  // The BodyDirector's counts over every preset win over its list (filtered by the UI and limited in length).
  if (const auto& counts = obj(in, "presetCounts"); counts.contains("total"))
    for (const char* key : {"total", "compatible", "experimental", "needsResources", "blocked"}) presets[key] = num(counts, key);
  out["presets"] = presets;
  for (const auto& change : arr(npc, "changes"))
    out["familyChanges"].push_back({{"target", str(change, "target")}, {"label", str(change, "label")},
                                    {"verdict", str(change, "verdict")}, {"verdictLabel", str(change, "verdictLabel")}});

  // ---- Outfit: the draft of the same NPC, its slot conflicts and the worker validation
  const auto& outfit = obj(in, "outfit");
  const bool outfit_same = str(outfit, "formKey") == form_key;
  if (outfit_same) {
    for (const auto& c : arr(outfit, "localConflicts"))
      conflicts.push_back({{"kind", "slot"}, {"text", "Slot " + std::to_string(num(c, "slot")) + ": " + str(c, "first") + " y " + str(c, "second")}});
    for (const auto& issue : arr(obj(outfit, "validation"), "issues")) {
      const auto code = str(issue, "code");
      const auto state = std::string(state_of_severity(str(issue, "severity")));
      if (code == "SlotConflict" || code == "SlotConflictInherited") conflicts.push_back({{"kind", "slot"}, {"text", str(issue, "message")}});
      if (is_missing_code(code)) missing.push_back(str(issue, "message"));
      findings.push_back({{"code", code}, {"uiState", state}, {"resource", "Atuendo"}, {"problem", str(issue, "message")},
                          {"operation", ""}, {"blocksExport", state == kBlocking}});
    }
  }

  // ---- Garments: only what the Fit Doctor analysed for THIS NPC
  const auto& fit = obj(in, "fit");
  auto& garments = out["outfits"];
  const bool fit_same = str(fit, "formKey") == form_key && !arr(fit, "pieces").empty();
  if (fit_same) {
    garments["verified"] = true;
    const auto body_family = str(fit, "bodyFamily");
    for (const auto& piece : arr(fit, "pieces")) {
      const auto name = str(piece, "armorName");
      const auto family = str(piece, "detectedFamily");
      const auto confidence = str(piece, "confidence");
      if (flag(piece, "isOverwrite"))
        conflicts.push_back({{"kind", "file"}, {"text", name + ": la malla que gana viene de overwrite de MO2 (" + str(piece, "winningProvider") + ")."}});
      if (confidence == "incompatible-confirmed" || (known_family(family) && known_family(body_family) && lower(family) != lower(body_family))) {
        garments["needsConversion"].push_back({{"name", name}, {"reason", "Hecha para " + family + "; el cuerpo es " + body_family + "."}});
      } else if (known_family(family) && known_family(body_family) && flag(piece, "topologyCompatible") && flag(piece, "skeletonCompatible") &&
                 (confidence == "confirmed-geometric" || confidence == "probable-metadata")) {
        garments["compatible"].push_back(name);
      } else {
        garments["unverified"].push_back(name);
      }
    }
  } else if (outfit_same) {
    for (const auto& piece : arr(outfit, "pieces")) garments["unverified"].push_back(str(piece, "name"));
  }
  if (fit_same) {
    // The two analysers must agree on the body before any garment verdict is trusted.
    const auto director = str(npc, "familyLabel");
    const auto doctor = str(fit, "bodyFamily");
    if (known_family(director) && known_family(doctor) && family_key(director) != family_key(doctor))
      findings.push_back({{"code", "bodyFamilyDisagreement"}, {"uiState", std::string(kManualReview)}, {"resource", "Cuerpo"},
                          {"problem", "Los diagnósticos discrepan sobre la familia del cuerpo: BodyDirector dice " + director +
                                          ", Outfit Fit Doctor dice " + doctor + "."},
                          {"operation", "Revisa el cuerpo que gana en el Creador (pestaña Cuerpo) antes de elegir prendas por familia."},
                          {"blocksExport", false}});
    if (const auto n = garments["unverified"].size(); n > 0)
      findings.push_back({{"code", "garmentsIndeterminate"}, {"uiState", std::string(kNotVerified)}, {"resource", "Prendas"},
                          {"problem", "Outfit Fit Doctor no pudo determinar la familia corporal de " + std::to_string(n) +
                                          " prenda(s) (confianza indeterminada): no se llaman compatibles."},
                          {"operation", "Revisa el detalle de cada prenda en el Outfit Fit Doctor del Estudio de atuendos."},
                          {"blocksExport", false}});
  }
  if (!garments["needsConversion"].empty())
    findings.push_back({{"code", "garmentFamily"}, {"uiState", std::string(kManualReview)}, {"resource", "Prendas"},
                        {"problem", std::to_string(garments["needsConversion"].size()) + " prenda(s) de otra familia corporal."},
                        {"operation", "Elige una alternativa de la familia del cuerpo en Outfit Fit Doctor; la conversión de prendas no está implementada."},
                        {"blocksExport", false}});
  if (!fit_same)
    findings.push_back({{"code", "garmentsNotAnalysed"}, {"uiState", std::string(kNotVerified)}, {"resource", "Prendas"},
                        {"problem", "Outfit Fit Doctor no ha analizado este NPC: la compatibilidad de las prendas no está verificada."},
                        {"operation", "Abre el Estudio de atuendos y ejecuta el diagnóstico."}, {"blocksExport", false}});

  // ---- Environment: what the export paths need
  const auto& env = obj(in, "environment");
  if (str(env, "state") == "ready" && str(env, "bcng").empty())
    missing.push_back("Body Change NG no está en el perfil: los presets se pueden previsualizar, no distribuir.");
  if (str(env, "state") == "ready" && num(env, "presets") == 0) missing.push_back("No hay presets BodySlide en el perfil.");

  // ---- Export routes: each one is the existing operation, with its own checks
  auto& exports = out["exports"];
  {
    Json e{{"id", "outfitPatch"}, {"label", "Parche de atuendo (Safe Patch)"}, {"target", "outfit"}};
    const auto& validation = obj(outfit, "validation");
    if (!outfit_same) e.update({{"state", "blocked"}, {"reason", "El atuendo del Estudio de atuendos es de otro NPC."}});
    else if (flag(outfit, "validating")) e.update({{"state", "partial"}, {"reason", "Validando el atuendo con el worker…"}});
    else if (!validation.contains("exportable")) e.update({{"state", "partial"}, {"reason", "Valida el atuendo antes de exportarlo."}});
    else if (!flag(validation, "exportable")) e.update({{"state", "blocked"}, {"reason", "La validación del worker tiene errores (ver hallazgos)."}});
    else e.update({{"state", "available"}, {"reason", "Validado por el worker; se exporta a una carpeta nueva con relectura."}});
    exports.push_back(e);
  }
  {
    Json e{{"id", "bodyPreset"}, {"label", "Preset de cuerpo (Body Change NG)"}, {"target", "creator.body"}};
    if (str(env, "state") == "ready" && str(env, "bcng").empty()) e.update({{"state", "blocked"}, {"reason", "Body Change NG no está instalado en el perfil."}});
    else if (presets["compatible"].get<int>() == 0)
      e.update({{"state", "blocked"}, {"reason", "Ningún preset es compatible con la familia de este cuerpo."}});
    else e.update({{"state", "available"}, {"reason", std::to_string(presets["compatible"].get<int>()) + " preset(s) de la misma familia; regla nueva en un mod aparte."}});
    exports.push_back(e);
  }
  {
    Json e{{"id", "familyChange"}, {"label", "Cambiar de familia (CBBE/3BA ↔ UBE)"}, {"target", "creator.body"}, {"state", "blocked"},
           {"reason", "Solo evaluación: el cambio de familia no se exporta (pesos, TRI y prendas no son intercambiables)."}};
    exports.push_back(e);
  }
  {
    Json e{{"id", "fitRepair"}, {"label", "Reparar prendas (Outfit Fit Doctor)"}, {"target", "outfit"}};
    const auto fixable = num(fit, "fixableAlternatives");
    if (!fit_same) e.update({{"state", "partial"}, {"reason", "Ejecuta Outfit Fit Doctor para este NPC."}});
    else if (garments["needsConversion"].empty() && !garments["unverified"].empty())
      e.update({{"state", "partial"}, {"reason", std::to_string(garments["unverified"].size()) +
                                                     " prenda(s) sin familia determinada: no se puede decidir si hace falta reparar."}});
    else if (garments["needsConversion"].empty()) e.update({{"state", "notNeeded"}, {"reason", "Todas las prendas analizadas son de la familia del cuerpo."}});
    else if (fixable == 0) e.update({{"state", "blocked"}, {"reason", "No hay alternativas compatibles instaladas; la conversión no está implementada."}});
    else e.update({{"state", "available"}, {"reason", std::to_string(fixable) + " alternativa(s) compatibles instaladas."}});
    exports.push_back(e);
  }
  exports.push_back({{"id", "face"}, {"label", "Rostro y apariencia (Creador de mods fácil)"}, {"target", "creator"}, {"state", "partial"},
                     {"reason", "Lo decide el Creador de mods fácil con sus comprobaciones de FaceGen; aquí no se genera FaceGen nuevo."}});
  return out;
}

std::string project_folder_name(std::string_view name) {
  if (const auto sep = name.find_last_of("/\\"); sep != std::string_view::npos) name = name.substr(sep + 1);
  if (const auto dot = name.rfind('.'); dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
  std::string out;
  for (const char c : name) {
    const auto u = static_cast<unsigned char>(c);
    const bool keep = u < 0x80 && (std::isalnum(u) || c == '_' || c == '-');
    if (keep) out.push_back(c);
    else if (!out.empty() && out.back() != '_') out.push_back('_');
    if (out.size() >= 48) break;
  }
  while (!out.empty() && (out.back() == '_' || out.back() == '-')) out.pop_back();
  while (!out.empty() && (out.front() == '_' || out.front() == '-')) out.erase(out.begin());
  if (out.empty()) out = "personaje";
  return out + ".sncf";
}

}  // namespace sns::creation::easy
