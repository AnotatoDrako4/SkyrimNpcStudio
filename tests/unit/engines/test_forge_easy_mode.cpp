#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "sns/creation/forge_easy_mode.hpp"
#include "sns/creation/skyrim_conversion.hpp"

namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace easy = sns::creation::easy;

namespace {

// goblet_report.json: the Blender report of the synthetic prop of make_test_models.py skyrim_prop (no third-party asset).
Json goblet_classification() {
  std::ifstream in(fs::path(SNS_TEST_MODELS_DIR) / "skyrim" / "goblet_report.json", std::ios::binary);
  REQUIRE(in);
  const auto report = Json::parse(std::string(std::istreambuf_iterator<char>(in), {}));
  return sns::creation::skyrim::classify(report, "static");
}

Json issue(const char* code, const char* severity, bool fix = false) {
  return {{"code", code}, {"severity", severity}, {"object", "Mesh"}, {"problem", "p"}, {"consequence", "c"},
          {"solution", "s"}, {"nonDestructive", true}, {"fixAvailable", fix}};
}

Json classification(Json issues, bool exportable, const char* profile = "static") {
  return {{"profile", profile}, {"issues", std::move(issues)}, {"exportable", exportable}};
}

Json by_id(const Json& list, const std::string& id) {
  Json found;
  for (const auto& item : list)
    if (item.at("id") == id) found = item;
  if (found.is_null()) FAIL("missing id " << id);
  return found;
}

bool lists(const Json& missing, const std::string& text) {
  for (const auto& m : missing)
    if (m.get<std::string>().find(text) != std::string::npos) return true;
  return false;
}

bool says(const Json& action, const char* key, const std::string& text) {
  return action.at(key).get<std::string>().find(text) != std::string::npos;
}

// Every fact of a static resource whose NIF was exported and verified, with the load order ready.
easy::Facts ready_static() {
  easy::Facts f;
  f.project_open = f.project_saved = f.blender_found = f.worker_ready = f.profile_indexed = true;
  f.model_selected = f.preview_loaded = true;
  f.model_format = "blend";
  f.preview_state = "loaded";
  f.conversion_state = "exported";
  f.classification = goblet_classification();
  f.nif_verified = true;
  return f;
}

}  // namespace

TEST_CASE("Easy mode maps every classify severity to one UI state", "[easy]") {
  CHECK(easy::ui_state(issue("a", "blocking")) == easy::kBlocking);
  CHECK(easy::ui_state(issue("a", "autoFixable", true)) == easy::kSafeFix);
  // An automatic fix the job does not really perform is not offered as safe.
  CHECK(easy::ui_state(issue("a", "autoFixable", false)) == easy::kManualReview);
  CHECK(easy::ui_state(issue("a", "warning")) == easy::kManualReview);
  CHECK(easy::ui_state(issue("a", "manualReview")) == easy::kManualReview);
  CHECK(easy::ui_state(issue("a", "compatible")) == easy::kCompatible);
  CHECK(easy::ui_state(issue("a", "somethingNew")) == easy::kNotVerified);
  CHECK(easy::ui_state(Json()) == easy::kNotVerified);
}

TEST_CASE("Easy mode explains the real goblet diagnosis without hiding what is not verified", "[easy]") {
  const auto classified = goblet_classification();
  const auto explained = easy::explain(classified);
  CHECK(explained.at("exportable") == classified.at("exportable"));
  CHECK(explained.at("state") != std::string(easy::kBlocking));
  int counted = 0;
  bool in_game = false, collision = false;
  for (const auto& f : explained.at("findings")) {
    for (const char* key : {"code", "uiState", "resource", "problem", "whySkyrim", "operation", "reversible", "requiresUser", "blocksExport"})
      CHECK(f.contains(key));
    if (f.at("pending").get<bool>()) {
      CHECK(f.at("uiState") == std::string(easy::kNotVerified));
      CHECK_FALSE(f.at("blocksExport").get<bool>());
      in_game = in_game || f.at("code") == "inGame";
      collision = collision || f.at("code") == "collision";
    } else {
      ++counted;
      CHECK(f.at("blocksExport").get<bool>() == (f.at("uiState") == std::string(easy::kBlocking)));
    }
  }
  CHECK(counted == static_cast<int>(classified.at("issues").size()));
  CHECK(in_game);
  CHECK(collision);
  int total = 0;
  for (const auto& [state, n] : explained.at("counts").items()) total += n.get<int>();
  CHECK(total == counted);
}

TEST_CASE("Easy mode diagnosis: blocking wins, and no analysis is NO_VERIFICADO", "[easy]") {
  const auto blocked = easy::explain(classification(
      Json::array({issue("shapeTooBig", "autoFixable", true), issue("missingUv", "blocking"), issue("noMaterial", "warning")}), false));
  CHECK(blocked.at("state") == std::string(easy::kBlocking));
  CHECK_FALSE(blocked.at("exportable").get<bool>());
  CHECK(blocked.at("counts").at(std::string(easy::kBlocking)) == 1);
  CHECK(blocked.at("counts").at(std::string(easy::kSafeFix)) == 1);
  CHECK(blocked.at("counts").at(std::string(easy::kManualReview)) == 1);
  bool uv_seen = false;
  for (const auto& f : blocked.at("findings"))
    if (f.at("code") == "missingUv") {
      uv_seen = true;
      CHECK(f.at("requiresUser").get<bool>());
      CHECK(f.at("blocksExport").get<bool>());
      CHECK(f.at("resource") == "Mesh");
    }
  CHECK(uv_seen);

  CHECK(easy::explain(classification(Json::array({issue("suspiciousScale", "autoFixable", true)}), true)).at("state") ==
        std::string(easy::kSafeFix));
  CHECK(easy::explain(classification(Json::array(), true)).at("state") == std::string(easy::kCompatible));

  for (const auto& none : {Json(), Json::object()}) {
    const auto e = easy::explain(none);
    CHECK(e.at("state") == std::string(easy::kNotVerified));
    CHECK_FALSE(e.at("exportable").get<bool>());
    CHECK(e.at("findings").empty());
  }
}

TEST_CASE("Easy mode actions with nothing ready say exactly what is missing", "[easy]") {
  const auto a = easy::actions(easy::Facts{});
  REQUIRE(a.size() == 7);
  for (const auto* id : {"editNpc", "bodyAppearance", "outfits"}) {
    const auto action = by_id(a, id);
    CHECK(action.at("state") == "blocked");
    CHECK(lists(action.at("missing"), "indexado"));
    CHECK_FALSE(action.at("next").get<std::string>().empty());
  }
  CHECK(by_id(a, "createCharacter").at("state") == "partial");
  CHECK(says(by_id(a, "createCharacter"), "reason", "FaceGen"));  // the NPC route never claims a face it cannot build
  CHECK(by_id(a, "importBlender").at("state") == "partial");
  CHECK(lists(by_id(a, "importBlender").at("missing"), "Blender"));
  const auto prepare = by_id(a, "prepareResource");
  CHECK(prepare.at("state") == "blocked");
  CHECK(lists(prepare.at("missing"), "modelo"));
  const auto mod = by_id(a, "exportMod");
  CHECK(mod.at("state") == "blocked");
  CHECK(lists(mod.at("missing"), "NIF"));
  CHECK(lists(mod.at("missing"), "BethesdaWorker"));
}

TEST_CASE("Easy mode actions with the load order ready open NPC, body and outfit routes without overclaiming", "[easy]") {
  easy::Facts f;
  f.worker_ready = f.profile_indexed = true;
  const auto a = easy::actions(f);
  CHECK(by_id(a, "editNpc").at("state") == "available");
  CHECK(by_id(a, "bodyAppearance").at("state") == "partial");
  CHECK(says(by_id(a, "bodyAppearance"), "reason", "UBE"));
  CHECK(by_id(a, "outfits").at("state") == "partial");
  CHECK(by_id(a, "editNpc").at("target") == "creator");
  CHECK(by_id(a, "outfits").at("target") == "outfit");
}

TEST_CASE("Easy mode export is available only when every check of its route passed", "[easy]") {
  const auto full = ready_static();
  CHECK(by_id(easy::actions(full), "exportMod").at("state") == "available");
  CHECK(by_id(easy::actions(full), "exportMod").at("missing").empty());

  auto no_worker = full;
  no_worker.worker_ready = false;
  no_worker.worker_message = "Iniciando";
  const auto w = by_id(easy::actions(no_worker), "exportMod");
  CHECK(w.at("state") == "blocked");
  CHECK(says(w, "reason", "Iniciando"));

  auto no_index = full;
  no_index.profile_indexed = false;
  CHECK(by_id(easy::actions(no_index), "exportMod").at("state") == "blocked");

  auto unverified = full;
  unverified.nif_verified = false;
  unverified.conversion_state = "verificationFailed";
  CHECK(by_id(easy::actions(unverified), "exportMod").at("state") == "blocked");

  auto analyzed = full;
  analyzed.conversion_state = "analyzed";
  analyzed.nif_verified = false;
  const auto a = by_id(easy::actions(analyzed), "exportMod");
  CHECK(a.at("state") == "blocked");
  CHECK(says(a, "next", "Preparar"));

  auto blocking = full;
  blocking.conversion_state = "analyzed";
  blocking.classification = classification(Json::array({issue("missingUv", "blocking")}), false);
  CHECK(says(by_id(easy::actions(blocking), "exportMod"), "reason", "bloqueante"));

  auto done = full;
  done.mod_state = "done";
  const auto d = by_id(easy::actions(done), "exportMod");
  CHECK(d.at("state") == "available");
  CHECK(says(d, "reason", "PENDIENTE_SKYRIM"));
}

TEST_CASE("Easy mode never routes a skinned character to the STAT mod", "[easy]") {
  auto rigged = ready_static();
  rigged.conversion_state = "analyzed";
  rigged.nif_verified = false;
  rigged.classification = classification(Json::array({issue("skinnedStatic", "blocking")}), false);
  const auto a = easy::actions(rigged);
  CHECK(by_id(a, "prepareResource").at("state") == "partial");
  CHECK(says(by_id(a, "prepareResource"), "reason", "skinned"));
  CHECK(by_id(a, "exportMod").at("state") == "blocked");
  CHECK(says(by_id(a, "exportMod"), "reason", "ARMO"));

  auto skinned = ready_static();
  skinned.profile = "skinned";
  skinned.classification = classification(Json::array(), true, "skinned");
  CHECK(by_id(easy::actions(skinned), "exportMod").at("state") == "blocked");
  const auto f = easy::flow(skinned);
  CHECK(f.at("current") == "export");
  CHECK(f.at("steps").at(5).at("state") == "blocked");
  CHECK_FALSE(f.at("canExport").get<bool>());
}

TEST_CASE("Easy mode flow walks SELECCIONAR to EXPORTAR from the backend facts", "[easy]") {
  easy::Facts f;
  auto flow = easy::flow(f);
  REQUIRE(flow.at("steps").size() == 6);
  CHECK(flow.at("current") == "select");
  for (std::size_t i = 1; i < 6; ++i) CHECK(flow.at("steps").at(i).at("state") == "pending");

  f.model_selected = true;
  f.model_format = "blend";
  f.blender_found = true;
  f.preview_state = "loading";
  CHECK(easy::flow(f).at("current") == "preview");
  CHECK(easy::flow(f).at("steps").at(1).at("state") == "running");
  CHECK(easy::flow(f).at("steps").at(2).at("state") == "pending");  // one step at a time (seen in the window: two "current")
  f.preview_state = "idle";  // preview not started yet: it is the current step, the diagnosis waits
  CHECK(easy::flow(f).at("steps").at(1).at("state") == "current");
  CHECK(easy::flow(f).at("steps").at(2).at("state") == "pending");
  int current_steps = 0;
  const auto waiting = easy::flow(f);  // a named value: at() of a temporary would dangle inside the range-for
  for (const auto& s : waiting.at("steps")) current_steps += s.at("state") == "current" || s.at("state") == "running";
  CHECK(current_steps == 1);

  f.preview_state = "loaded";
  f.preview_loaded = true;
  CHECK(easy::flow(f).at("current") == "diagnose");

  f.conversion_state = "running";
  f.conversion_operation = "inspect";
  CHECK(easy::flow(f).at("steps").at(2).at("state") == "running");

  f.conversion_state = "analyzed";
  f.classification = goblet_classification();
  REQUIRE(f.classification.at("exportable").get<bool>());
  CHECK(easy::flow(f).at("current") == "prepare");

  f.conversion_state = "running";
  f.conversion_operation = "exportNif";
  CHECK(easy::flow(f).at("steps").at(2).at("state") == "done");
  CHECK(easy::flow(f).at("steps").at(3).at("state") == "running");
  f.conversion_state = "error";
  CHECK(easy::flow(f).at("current") == "prepare");
  CHECK(easy::flow(f).at("steps").at(3).at("state") == "blocked");

  f.conversion_state = "exported";
  f.nif_verified = true;
  // Load order not ready: export is blocked with the reason, never offered
  flow = easy::flow(f);
  CHECK(flow.at("current") == "export");
  CHECK(flow.at("steps").at(5).at("state") == "blocked");
  CHECK_FALSE(flow.at("canExport").get<bool>());

  f.worker_ready = f.profile_indexed = true;
  flow = easy::flow(f);
  CHECK(flow.at("steps").at(4).at("state") == "done");
  CHECK(flow.at("steps").at(5).at("state") == "current");
  CHECK(flow.at("canExport").get<bool>());
  f.busy = true;
  CHECK_FALSE(easy::flow(f).at("canExport").get<bool>());
  f.busy = false;

  f.mod_state = "running";
  CHECK(easy::flow(f).at("steps").at(5).at("state") == "running");
  f.mod_state = "done";
  flow = easy::flow(f);
  for (const auto& s : flow.at("steps")) CHECK(s.at("state") == "done");
  CHECK(flow.at("current") == "");
  CHECK(flow.at("steps").at(5).at("detail").get<std::string>().find("PENDIENTE_SKYRIM") != std::string::npos);
}

TEST_CASE("Easy mode flow: a failed preview does not hold the diagnosis, a blocking issue holds the rest", "[easy]") {
  easy::Facts f;
  f.model_selected = f.blender_found = true;
  f.model_format = "blend";
  f.preview_state = "error";
  auto flow = easy::flow(f);
  CHECK(flow.at("steps").at(1).at("state") == "blocked");
  CHECK(flow.at("current") == "diagnose");

  f.conversion_state = "analyzed";
  f.classification = classification(Json::array({issue("missingUv", "blocking")}), false);
  flow = easy::flow(f);
  CHECK(flow.at("current") == "diagnose");
  CHECK(flow.at("steps").at(2).at("state") == "blocked");
  for (std::size_t i = 3; i < 6; ++i) CHECK(flow.at("steps").at(i).at("state") == "pending");

  f.classification = goblet_classification();
  f.nif_verified = false;
  f.conversion_state = "verificationFailed";
  flow = easy::flow(f);
  CHECK(flow.at("current") == "validate");
  CHECK(flow.at("steps").at(4).at("state") == "blocked");

  easy::Facts no_blender;
  no_blender.model_selected = true;
  no_blender.model_format = "fbx";
  flow = easy::flow(no_blender);
  CHECK(flow.at("steps").at(1).at("state") == "blocked");
  CHECK(flow.at("steps").at(2).at("state") == "blocked");
  CHECK(flow.at("current") == "diagnose");
}

TEST_CASE("Easy mode project folder names are portable and never empty", "[easy]") {
  CHECK(easy::project_folder_name("Meshy_AI_Female_Robot.blend") == "Meshy_AI_Female_Robot.sncf");
  CHECK(easy::project_folder_name("Sarah (TLOU) 00X.blend") == "Sarah_TLOU_00X.sncf");
  CHECK(easy::project_folder_name("Copa de oro.fbx") == "Copa_de_oro.sncf");
  CHECK(easy::project_folder_name("\xC3\x91" "and\xC3\xBA.glb") == "and.sncf");  // Ñandú
  CHECK(easy::project_folder_name("..\\..\\Windows") == "Windows.sncf");
  CHECK(easy::project_folder_name("G:/Descargas/robot.v2.blend") == "robot_v2.sncf");
  CHECK(easy::project_folder_name("") == "personaje.sncf");
  CHECK(easy::project_folder_name("???") == "personaje.sncf");
  CHECK(easy::project_folder_name(".blend") == "blend.sncf");
  CHECK(easy::project_folder_name(std::string(200, 'a')).size() == 48 + 5);
}

namespace {

// A ready BodyDirector result for a 3BA NPC, the presets with their verdicts, an outfit of the same NPC and a Fit Doctor run.
Json npc_input() {
  return {{"indexed", true},
          {"npc", {{"state", "ready"}, {"formKey", "0A2C8E:Skyrim.esm"}, {"name", "Lydia"}, {"family", "cbbe3ba"}, {"familyLabel", "CBBE 3BA"},
                   {"body", {{"path", "meshes\\actors\\character\\character assets\\femalebody_1.nif"}, {"provider", "3BA"}, {"tri", "femalebody.tri"}}},
                   {"issues", Json::array({{{"severity", "info"}, {"code", "SkyPatcherPresent"}, {"message", "SkyPatcher"}}})},
                   {"changes", Json::array({{{"target", "ube"}, {"label", "UBE"}, {"verdict", "experimental"}, {"verdictLabel", "Experimental"}}})}}},
          {"environment", {{"state", "ready"}, {"bcng", "Body Change NG"}, {"presets", 12}}},
          {"presets", Json::array({{{"verdict", "compatible"}}, {{"verdict", "compatible"}}, {{"verdict", "experimental"}},
                                   {{"verdict", "needs-resources"}}, {{"verdict", "blocked"}}})},
          {"outfit", {{"formKey", "0A2C8E:Skyrim.esm"}, {"pieces", Json::array({{{"name", "Steel Armor"}}, {{"name", "UBE Dress"}}})},
                      {"localConflicts", Json::array()}, {"validating", false},
                      {"validation", {{"exportable", true}, {"issues", Json::array()}}}}},
          {"fit", {{"formKey", "0A2C8E:Skyrim.esm"}, {"bodyFamily", "CBBE 3BA"}, {"fixableAlternatives", 1},
                   {"pieces", Json::array({{{"armorName", "Steel Armor"}, {"detectedFamily", "CBBE 3BA"}, {"confidence", "confirmed-geometric"},
                                            {"topologyCompatible", true}, {"skeletonCompatible", true}, {"isOverwrite", false}},
                                           {{"armorName", "UBE Dress"}, {"detectedFamily", "UBE"}, {"confidence", "probable-metadata"},
                                            {"topologyCompatible", false}, {"skeletonCompatible", true}, {"isOverwrite", true},
                                            {"winningProvider", "Overwrite"}},
                                           {{"armorName", "Ring"}, {"detectedFamily", "Unknown"}, {"confidence", "indeterminate"},
                                            {"topologyCompatible", true}, {"skeletonCompatible", true}}})}}}};
}

Json export_of(const Json& overview, const std::string& id) {
  Json found;
  for (const auto& e : overview.at("exports"))
    if (e.at("id") == id) found = e;
  if (found.is_null()) FAIL("missing export " << id);
  return found;
}

}  // namespace

TEST_CASE("Easy mode NPC overview waits for an indexed profile and a selected NPC", "[easy][npc]") {
  CHECK(easy::npc_overview(Json()).at("state") == "none");
  auto in = npc_input();
  in["indexed"] = false;
  CHECK(easy::npc_overview(in).at("state") == "none");
  in = npc_input();
  in["npc"] = {{"state", "inspecting"}, {"formKey", "0A2C8E:Skyrim.esm"}};
  CHECK(easy::npc_overview(in).at("state") == "loading");
  in["npc"] = {{"state", "error"}, {"formKey", "0A2C8E:Skyrim.esm"}, {"error", "boom"}};
  const auto e = easy::npc_overview(in);
  CHECK(e.at("state") == "error");
  CHECK(e.at("message").get<std::string>().find("boom") != std::string::npos);
}

TEST_CASE("Easy mode NPC overview reports body, presets, garments, conflicts and exports from existing results", "[easy][npc]") {
  const auto o = easy::npc_overview(npc_input());
  REQUIRE(o.at("state") == "ready");
  CHECK(o.at("body").at("familyLabel") == "CBBE 3BA");
  CHECK(o.at("body").at("provider") == "3BA");
  CHECK(o.at("body").at("uiState") == std::string(easy::kCompatible));
  CHECK(o.at("presets").at("total") == 5);
  CHECK(o.at("presets").at("compatible") == 2);
  CHECK(o.at("presets").at("experimental") == 1);
  CHECK(o.at("presets").at("needsResources") == 1);
  CHECK(o.at("familyChanges").size() == 1);
  // Garments: only the Fit Doctor's confirmed piece is compatible; UBE on a 3BA body needs conversion; unknown stays unverified
  CHECK(o.at("outfits").at("verified").get<bool>());
  CHECK(o.at("outfits").at("compatible") == Json::array({"Steel Armor"}));
  REQUIRE(o.at("outfits").at("needsConversion").size() == 1);
  CHECK(o.at("outfits").at("needsConversion").at(0).at("name") == "UBE Dress");
  CHECK(o.at("outfits").at("unverified") == Json::array({"Ring"}));
  REQUIRE(o.at("conflicts").size() == 1);
  CHECK(o.at("conflicts").at(0).at("kind") == "file");
  CHECK(o.at("missing").empty());
  CHECK(export_of(o, "outfitPatch").at("state") == "available");
  CHECK(export_of(o, "bodyPreset").at("state") == "available");
  CHECK(export_of(o, "familyChange").at("state") == "blocked");  // never exported

  // Nothing of another family: "not needed" only when every garment was determined; indeterminate ones keep it partial
  auto same = npc_input();
  same["fit"]["pieces"] = Json::array({same["fit"]["pieces"][0]});
  CHECK(export_of(easy::npc_overview(same), "fitRepair").at("state") == "notNeeded");
  same["fit"]["pieces"].push_back(npc_input()["fit"]["pieces"][2]);
  CHECK(export_of(easy::npc_overview(same), "fitRepair").at("state") == "partial");
  CHECK(export_of(o, "fitRepair").at("state") == "available");
  CHECK(export_of(o, "face").at("state") == "partial");

  // Agreement: "CBBE 3BA" from both analysers raises no disagreement; the indeterminate ring is explained
  bool disagreement = false, indeterminate = false;
  for (const auto& f : o.at("findings")) {
    disagreement = disagreement || f.at("code") == "bodyFamilyDisagreement";
    indeterminate = indeterminate || f.at("code") == "garmentsIndeterminate";
  }
  CHECK_FALSE(disagreement);
  CHECK(indeterminate);

  // Counts over every preset win over the (filtered, limited) list
  auto in = npc_input();
  in["presetCounts"] = {{"total", 640}, {"compatible", 0}, {"experimental", 3}, {"needsResources", 7}, {"blocked", 630}};
  const auto all = easy::npc_overview(in);
  CHECK(all.at("presets").at("total") == 640);
  CHECK(all.at("presets").at("compatible") == 0);
  CHECK(export_of(all, "bodyPreset").at("state") == "blocked");
}

TEST_CASE("Easy mode NPC overview never assumes garment compatibility it did not verify", "[easy][npc]") {
  auto in = npc_input();
  in["fit"]["formKey"] = "000001:Other.esp";  // the Fit Doctor ran for another NPC
  const auto o = easy::npc_overview(in);
  CHECK_FALSE(o.at("outfits").at("verified").get<bool>());
  CHECK(o.at("outfits").at("compatible").empty());
  CHECK(o.at("outfits").at("unverified").size() == 2);  // the outfit's own pieces, unverified
  bool flagged = false;
  for (const auto& f : o.at("findings")) flagged = flagged || (f.at("code") == "garmentsNotAnalysed" && f.at("uiState") == std::string(easy::kNotVerified));
  CHECK(flagged);
  CHECK(export_of(o, "fitRepair").at("state") == "partial");

  // A body family the analysis could not determine is NO_VERIFICADO, and no garment is called compatible
  in = npc_input();
  in["npc"]["family"] = "unknown";
  in["fit"]["bodyFamily"] = "Unknown";
  const auto u = easy::npc_overview(in);
  CHECK(u.at("body").at("uiState") == std::string(easy::kNotVerified));
  CHECK(u.at("outfits").at("compatible").empty());
}

TEST_CASE("Easy mode NPC overview reports when the two body analysers disagree", "[easy][npc]") {
  auto in = npc_input();
  in["fit"]["bodyFamily"] = "CBBE";  // BodyDirector says CBBE 3BA (seen with Lydia on the real profile)
  const auto o = easy::npc_overview(in);
  bool found = false;
  for (const auto& f : o.at("findings"))
    if (f.at("code") == "bodyFamilyDisagreement") {
      found = true;
      CHECK(f.at("uiState") == std::string(easy::kManualReview));
      CHECK(f.at("problem").get<std::string>().find("CBBE 3BA") != std::string::npos);
    }
  CHECK(found);
}

TEST_CASE("Easy mode NPC overview blocks exports whose own checks fail", "[easy][npc]") {
  auto in = npc_input();
  in["outfit"]["validation"] = {{"exportable", false},
                                {"issues", Json::array({{{"severity", "error"}, {"code", "SlotConflict"}, {"message", "Slot 32 dos veces"}},
                                                        {{"severity", "warning"}, {"code", "AddonNotFound"}, {"message", "Falta el ARMA de X"}}})}};
  in["outfit"]["localConflicts"] = Json::array({{{"slot", 32}, {"first", "A"}, {"second", "B"}}});
  in["environment"]["bcng"] = "";
  in["presets"] = Json::array({{{"verdict", "blocked"}}});
  in["fit"]["fixableAlternatives"] = 0;
  in["npc"]["issues"] = Json::array({{{"severity", "error"}, {"code", "OutfitMeshMissing"}, {"message", "Falta meshes\\x.nif"}}});
  const auto o = easy::npc_overview(in);
  CHECK(export_of(o, "outfitPatch").at("state") == "blocked");
  CHECK(export_of(o, "bodyPreset").at("state") == "blocked");
  CHECK(export_of(o, "fitRepair").at("state") == "blocked");
  CHECK(o.at("body").at("uiState") == std::string(easy::kBlocking));
  int slot_conflicts = 0;
  for (const auto& c : o.at("conflicts")) slot_conflicts += c.at("kind") == "slot";
  CHECK(slot_conflicts == 2);
  CHECK(o.at("missing").size() == 3);  // OutfitMeshMissing, AddonNotFound, Body Change NG

  // An outfit of another NPC is not exported for this one
  in = npc_input();
  in["outfit"]["formKey"] = "000002:Skyrim.esm";
  CHECK(export_of(easy::npc_overview(in), "outfitPatch").at("state") == "blocked");
  in = npc_input();
  in["outfit"]["validation"] = Json::object();
  CHECK(export_of(easy::npc_overview(in), "outfitPatch").at("state") == "partial");
}
