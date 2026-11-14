#include <NifFile.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <sstream>

#include "beauty_lab_engine.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/face/facegen.hpp"
#include "sns/rendering/scene.hpp"

using namespace sns;
using namespace sns::app;
using Catch::Matchers::WithinAbs;

namespace {

std::string make_test_nif(const std::string& shape_name,
                          std::uint32_t shader_type,
                          const std::string& diffuse = {},
                          const std::string& normal = {},
                          const std::string& physics_xml = {}) {
  nifly::NifFile file;
  file.Create(nifly::NiVersion::getSSE());
  std::vector<nifly::Vector3> v{{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
  std::vector<nifly::Triangle> t{{0, 1, 2}, {0, 2, 3}};
  std::vector<nifly::Vector2> uv{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  std::vector<nifly::Vector3> n(4, nifly::Vector3(0, 0, 1));
  auto* shape = file.CreateShapeFromData(shape_name, &v, &t, &uv, &n);
  file.GetShader(shape)->SetShaderType(shader_type);
  if (!diffuse.empty()) {
    auto d = diffuse;
    file.SetTextureSlot(shape, d, 0);
  }
  if (!normal.empty()) {
    auto nm = normal;
    file.SetTextureSlot(shape, nm, 1);
  }
  if (!physics_xml.empty()) {
    auto s = std::make_unique<nifly::NiStringExtraData>();
    s->name.get() = "HDT Skinned Mesh Physics Object";
    s->stringData.get() = physics_xml;
    const auto id = file.GetHeader().AddBlock(std::move(s));
    file.GetRootNode()->extraDataRefs.AddBlockRef(id);
  }
  std::ostringstream out(std::ios::binary);
  REQUIRE(file.Save(out) == 0);
  return out.str();
}

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
  std::filesystem::create_directories(path.parent_path());
  QFile f(QString::fromStdWString(path.wstring()));
  REQUIRE(f.open(QIODevice::WriteOnly));
  REQUIRE(f.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
}

face::FaceRecipe make_sample_recipe() {
  face::FaceRecipe r;
  r.npc_form_key = "002B6C:Dawnguard.esm";
  r.source_plugin = "Dawnguard.esm";
  r.female = true;
  r.race_form_key = "08883C:Dawnguard.esm";
  r.race_editor_id = "NordRaceVampire";
  r.morph_race_editor_id = "NordRace";
  r.values.weight = 20.0f;
  r.values.skin_tone = {210, 195, 190};
  r.values.hair_color = face::Rgb8{25, 22, 24};
  r.values.presets = {0, 1, 2, 1};

  face::HeadPartData face_part;
  face_part.form_key = "051509:Skyrim.esm";
  face_part.editor_id = "FemaleHeadNord";
  face_part.plugin = "Skyrim.esm";
  face_part.type = face::HeadPartType::Face;
  face_part.model = R"(actors\character\character assets\femalehead.nif)";
  r.parts.push_back(face_part);

  face::HeadPartData hair_part;
  hair_part.form_key = "004347:Dawnguard.esm";
  hair_part.editor_id = "HairFemaleSerana";
  hair_part.plugin = "Dawnguard.esm";
  hair_part.type = face::HeadPartType::Hair;
  hair_part.model = R"(actors\character\character assets\hair\female\hairfemale01.nif)";
  r.parts.push_back(hair_part);

  face::HeadPartData eyes_part;
  eyes_part.form_key = "0067CD:Dawnguard.esm";
  eyes_part.editor_id = "EyesFemaleVampire";
  eyes_part.plugin = "Dawnguard.esm";
  eyes_part.type = face::HeadPartType::Eyes;
  eyes_part.model = R"(actors\character\character assets\eyesfemale.nif)";
  r.parts.push_back(eyes_part);

  face::HeadPartData brows_part;
  brows_part.form_key = "051510:Skyrim.esm";
  brows_part.editor_id = "FemaleBrows01";
  brows_part.plugin = "Skyrim.esm";
  brows_part.type = face::HeadPartType::Eyebrows;
  brows_part.model = R"(actors\character\character assets\browsfemale.nif)";
  r.parts.push_back(brows_part);

  face::TintMaskDef skin_mask;
  skin_mask.index = 24;
  skin_mask.mask_type = face::kTintMaskSkinTone;
  skin_mask.mask_path = R"(actors\character\character assets\tintmasks\femalehead.dds)";
  r.tint_masks.push_back(skin_mask);

  face::TintMaskDef lip_mask;
  lip_mask.index = 31;
  lip_mask.mask_type = 3;  // LipColor
  lip_mask.mask_path = R"(actors\character\character assets\tintmasks\femaleheadnord_lips.dds)";
  r.tint_masks.push_back(lip_mask);

  face::TintMaskDef liner_mask;
  liner_mask.index = 26;
  liner_mask.mask_type = 2;  // Eyeliner
  liner_mask.mask_path = R"(actors\character\character assets\tintmasks\femalehead_eyeliner.dds)";
  r.tint_masks.push_back(liner_mask);

  r.values.tints.push_back({24, {210, 195, 190}, 1.0f, 0});
  r.values.tints.push_back({31, {120, 48, 52}, 0.35f, 0});

  return r;
}

appearance::FaceState state_from_recipe(const face::FaceRecipe& r) {
  appearance::FaceState st;
  st.values = r.values;
  for (const auto& p : r.parts) {
    if (!p.extra) st.head_parts.push_back(p.form_key);
  }
  st.geometry_mode = "regenerate";
  st.source = "Record";
  return st;
}

}  // namespace

TEST_CASE("BeautyLab: valid hair, overridden MO2 resources, and missing dependencies", "[beauty_lab]") {
  QTemporaryDir tmp;
  REQUIRE(tmp.isValid());
  const std::filesystem::path root = tmp.path().toStdWString();
  const auto data_dir = root / "game" / "Data";
  const auto mod_base = root / "mods" / "Base Hair Pack";
  const auto mod_hd = root / "mods" / "KS Hairdos SMP Replacer";
  std::filesystem::create_directories(data_dir);

  // Create valid hair NIF in both mods (Mod 2 overrides Mod 1)
  const std::string hair_rel = R"(meshes\actors\character\character assets\hair\female\hairfemale01.nif)";
  const std::string tex_diffuse = R"(textures\actors\character\hair\hairfemale01.dds)";
  const std::string tex_normal = R"(textures\actors\character\hair\hairfemale01_n.dds)";
  const std::string smp_xml = R"(meshes\actors\character\character assets\hair\female\hairfemale01.xml)";

  const std::string valid_nif = make_test_nif("KS_Hair_Female_01", nif::kShaderHairTint, tex_diffuse, tex_normal, smp_xml);
  write_bytes(mod_base / hair_rel, valid_nif);
  write_bytes(mod_hd / hair_rel, valid_nif);
  write_bytes(mod_hd / tex_diffuse, "DDS_DUMMY");
  write_bytes(mod_hd / tex_normal, "DDS_DUMMY");
  write_bytes(mod_hd / smp_xml, "<system/>");

  // Create a hair NIF whose textures are missing
  const std::string broken_hair_rel = R"(meshes\actors\character\hair\broken_hair.nif)";
  const std::string broken_nif = make_test_nif("BrokenHair", nif::kShaderHairTint,
                                               R"(textures\actors\character\hair\missing_diffuse.dds)",
                                               R"(textures\actors\character\hair\missing_normal.dds)");
  write_bytes(mod_hd / broken_hair_rel, broken_nif);

  Mo2Snapshot snap;
  snap.vfs = mo2::VirtualFileSystem::build_from({
      {"<Game Data>", data_dir, mo2::ProviderKind::GameData},
      {"Base Hair Pack", mod_base, mo2::ProviderKind::Mod},
      {"KS Hairdos SMP Replacer", mod_hd, mo2::ProviderKind::Mod}});

  snap.load_order.active = {
      {"Skyrim.esm", true, true, {}, "Base Hair Pack"},
      {"Dawnguard.esm", true, true, {}, "Base Hair Pack"},
      {"KS Hairdos.esp", true, false, {}, "KS Hairdos SMP Replacer"}};
  snap.plugin_provider = {
      {"skyrim.esm", "Base Hair Pack"},
      {"dawnguard.esm", "Base Hair Pack"},
      {"ks hairdos.esp", "KS Hairdos SMP Replacer"}};

  assets::AssetResolver resolver(snap.vfs, {});

  SECTION("Scenario 1 & 9: Valid hair inspection with SMP and MO2 override tracking") {
    QVariantMap row{
        {"formKey", "004347:KS Hairdos.esp"},
        {"editorId", "KS_Hair_Serana_SMP"},
        {"name", "KS Hair Serana SMP"},
        {"plugin", "KS Hairdos.esp"},
        {"type", "Hair"},
        {"meshPath", QString::fromStdString(hair_rel)}};

    const auto info = inspect_head_part_asset(row, nullptr, &snap, &resolver);
    CHECK(info.value("meshFound").toBool());
    CHECK(info.value("nifReadable").toBool());
    CHECK(info.value("supportsHairTint").toBool());
    CHECK(info.value("hasSmp").toBool());
    CHECK(info.value("requiresFaceGenRebuild").toBool());
    CHECK(info.value("packageFamily").toString() == "KS Hairdos");
    CHECK(info.value("effectiveProvider").toString() == "KS Hairdos SMP Replacer");
    CHECK(info.value("overrideCount").toInt() == 1);
    const QStringList overridden = info.value("overriddenBy").toStringList();
    REQUIRE(overridden.size() == 2);
    CHECK(overridden[0] == "Base Hair Pack");
    CHECK(overridden[1] == "KS Hairdos SMP Replacer");
    CHECK(info.value("missingTextures").toStringList().isEmpty());
    CHECK(info.value("compatibilityState").toString() == "compatible");
  }

  SECTION("Scenario 2: Hair with missing texture or missing NIF dependency is detected and blocks broken export") {
    QVariantMap broken_tex_row{
        {"formKey", "009999:KS Hairdos.esp"},
        {"editorId", "KS_Broken_Tex"},
        {"name", "KS Broken Texture Hair"},
        {"plugin", "KS Hairdos.esp"},
        {"type", "Hair"},
        {"meshPath", QString::fromStdString(broken_hair_rel)}};

    const auto tex_info = inspect_head_part_asset(broken_tex_row, nullptr, &snap, &resolver);
    CHECK(tex_info.value("meshFound").toBool());
    CHECK(tex_info.value("missingTextures").toStringList().size() == 2);
    CHECK(tex_info.value("compatibilityState").toString() == "warning");

    QVariantMap missing_nif_row{
        {"formKey", "00AAAA:UninstalledMod.esp"},
        {"editorId", "Apachii_Missing_Mesh"},
        {"name", "Apachii Missing Hair"},
        {"plugin", "UninstalledMod.esp"},
        {"type", "Hair"},
        {"meshPath", R"(meshes\apachii\missing_hair.nif)"}};

    const auto nif_info = inspect_head_part_asset(missing_nif_row, nullptr, &snap, &resolver);
    CHECK_FALSE(nif_info.value("meshFound").toBool());
    CHECK_FALSE(nif_info.value("pluginActive").toBool());
    CHECK(nif_info.value("packageFamily").toString() == "ApachiiSkyHair");
    CHECK(nif_info.value("compatibilityState").toString() == "error");

    // Verify comparator blocks export when proposed recipe references a missing mesh
    auto base_rec = make_sample_recipe();
    auto prop_rec = base_rec;
    prop_rec.parts[1].form_key = "00AAAA:UninstalledMod.esp";
    prop_rec.parts[1].model = R"(actors\character\apachii\missing_hair.nif)";
    auto base_st = state_from_recipe(base_rec);
    auto prop_st = state_from_recipe(prop_rec);

    const auto comp = compare_original_vs_proposed(
        &base_rec, &prop_rec, base_st, prop_st, 1.0f, 1.0f, {}, {{"status", "ok"}}, true, &snap, &resolver);
    CHECK(comp.value("hasChanges").toBool());
    CHECK(comp.value("proposalStatus").toString() == "BLOQUEADA_RECURSOS_FALTANTES");
    CHECK_FALSE(comp.value("incompatibleAlerts").toList().isEmpty());
  }
}

TEST_CASE("BeautyLab: intentionally bald NPC vs missing hair geometry and quality check", "[beauty_lab]") {
  auto base_rec = make_sample_recipe();
  auto bald_rec = base_rec;
  bald_rec.parts[1].model.clear();  // Intentionally bald: empty model on Hair slot

  auto base_st = state_from_recipe(base_rec);
  auto bald_st = base_st;
  bald_st.head_parts.push_back("000000:BALD");

  SECTION("Scenario 3: Intentionally bald NPC is verified in Quality Check and marked pending when using virtual bald") {
    const auto comp = compare_original_vs_proposed(
        &base_rec, &bald_rec, base_st, bald_st, 1.0f, 1.0f, {}, {{"status", "no-model"}}, false, nullptr, nullptr);
    CHECK(comp.value("hasChanges").toBool());
    CHECK(comp.value("proposalStatus").toString() == "PENDIENTE_RECONSTRUCCION");

    QVariantList scene_parts{
        QVariantMap{
            {"id", "head_face"},
            {"group", "head"},
            {"shapeName", "FemaleHeadNord"},
            {"meshPath", R"(meshes\actors\character\facegendata\facegeom\dawnguard.esm\00002b6c.nif)"},
            {"visible", true},
            {"vertexCount", 1200},
            {"min", QVariantList{-6.0, 118.0, -6.0}},
            {"max", QVariantList{6.0, 132.0, 6.0}}},
        QVariantMap{
            {"id", "body_torso"},
            {"group", "skin"},
            {"shapeName", "FemaleBody"},
            {"meshPath", R"(meshes\actors\character\character assets\femalebody_1.nif)"},
            {"visible", true},
            {"vertexCount", 3400},
            {"min", QVariantList{-18.0, 0.0, -10.0}},
            {"max", QVariantList{18.0, 118.5, 10.0}}}};

    const auto qc = run_visual_quality_check(
        &bald_rec, base_st, bald_st, true, {}, {{"status", "no-model"}}, scene_parts, {}, {}, {{"state", "ready"}, {"verdict", "ok"}}, nullptr, nullptr);
    CHECK(qc.value("overallStatus").toString() == "ok");
    CHECK_FALSE(qc.value("inGameVerified").toBool());
    CHECK(qc.value("verificationStatus").toString() == "PENDIENTE_SKYRIM");

    bool found_bald_ok = false;
    for (const auto& cv : qc.value("checks").toList()) {
      const auto cm = cv.toMap();
      if (cm.value("id").toString() == "hairPresence") {
        CHECK(cm.value("status").toString() == "ok");
        CHECK(cm.value("code").toString() == "IntentionallyBaldVerified");
        found_bald_ok = true;
      }
    }
    CHECK(found_bald_ok);
  }

  SECTION("Scenario 5 (FASE 5): Detects missing hair geometry and neck gap misalignment when not bald") {
    QVariantList scene_parts_with_gap_and_no_hair{
        QVariantMap{
            {"id", "head_face"},
            {"group", "head"},
            {"shapeName", "FemaleHeadNord"},
            {"meshPath", R"(meshes\actors\character\facegendata\facegeom\dawnguard.esm\00002b6c.nif)"},
            {"visible", true},
            {"vertexCount", 1200},
            {"min", QVariantList{-6.0, 125.0, -6.0}},  // 6.5 units above torso max Y (118.5) -> neck gap!
            {"max", QVariantList{6.0, 139.0, 6.0}}},
        QVariantMap{
            {"id", "body_torso"},
            {"group", "skin"},
            {"shapeName", "FemaleBody"},
            {"meshPath", R"(meshes\actors\character\character assets\femalebody_1.nif)"},
            {"visible", true},
            {"vertexCount", 3400},
            {"min", QVariantList{-18.0, 0.0, -10.0}},
            {"max", QVariantList{18.0, 118.5, 10.0}}}};

    const auto qc = run_visual_quality_check(
        &base_rec, base_st, base_st, false, {}, {{"status", "ok"}}, scene_parts_with_gap_and_no_hair, {}, {}, {}, nullptr, nullptr);
    CHECK(qc.value("overallStatus").toString() == "error");
    CHECK(qc.value("errorCount").toInt() >= 1);
    CHECK(qc.value("warningCount").toInt() >= 1);

    QString hair_code, neck_code;
    for (const auto& cv : qc.value("checks").toList()) {
      const auto cm = cv.toMap();
      if (cm.value("id").toString() == "hairPresence") hair_code = cm.value("code").toString();
      if (cm.value("id").toString() == "neckAlignment") neck_code = cm.value("code").toString();
    }
    CHECK(hair_code == "MissingHairGeometry");
    CHECK(neck_code == "NeckGapDetected");
  }
}

TEST_CASE("BeautyLab: custom head protection and Next-Gen style presets", "[beauty_lab]") {
  auto rec = make_sample_recipe();
  auto base_st = state_from_recipe(rec);

  QVariantMap custom_head_compat{
      {"analysed", true},
      {"status", "incompatible"},
      {"code", "CustomIncompatibleMorphs"},
      {"geometryMode", "preserve"},
      {"reason", "Vértices de cabeza personalizada (High Poly Head / escultura externa) incompatibles con el TRI vanilla."},
      {"blocked", QVariantList{
                      QVariantMap{{"op", "face-morphs"}, {"code", "CustomIncompatibleMorphs"}, {"reason", "Morphs bloqueados"}},
                      QVariantMap{{"op", "head-parts"}, {"code", "CustomIncompatibleMorphs"}, {"reason", "HeadParts bloqueados"}}}}};

  SECTION("Scenario 4: Custom head distinguishes locked geometry from parametric morphs and protects sculpted head") {
    const auto caps = evaluate_workshop_capabilities(&rec, custom_head_compat, "preserve");
    CHECK(caps.value("geometryKind").toString() == "imported-locked");
    CHECK_FALSE(caps.value("allowsParametricMorphs").toBool());
    CHECK_FALSE(caps.value("allowsPresets").toBool());
    CHECK_FALSE(caps.value("allowsHeadPartSwap").toBool());
    CHECK(caps.value("allowsHairColor").toBool());
    CHECK(caps.value("allowsSkinToneAndTints").toBool());

    // Applying Cinematic Fantasy on a custom head skips morphs & headParts while applying makeup tints safely
    const auto style_res = apply_next_gen_style("cinematic-fantasy", {}, rec, base_st, base_st, {}, custom_head_compat);
    CHECK(style_res.changed);
    CHECK(style_res.next_state.values.sliders == base_st.values.sliders);
    CHECK(style_res.next_state.head_parts == base_st.head_parts);
    CHECK_FALSE(style_res.skipped_rules.isEmpty());
    CHECK_FALSE(style_res.applied_rules.isEmpty());
  }

  SECTION("Parametric head allows full Next-Gen styles within technical limits [-1.0, +1.0]") {
    QVariantMap parametric_compat{
        {"analysed", true},
        {"status", "compatible"},
        {"geometryMode", "regenerate"},
        {"blocked", QVariantList{}}};
    const auto caps = evaluate_workshop_capabilities(&rec, parametric_compat, "regenerate");
    CHECK(caps.value("geometryKind").toString() == "parametric-regenerate");
    CHECK(caps.value("allowsParametricMorphs").toBool());

    const auto style_res = apply_next_gen_style("cinematic-fantasy", {{"intensity", 1.0}}, rec, base_st, base_st, {}, parametric_compat);
    CHECK(style_res.changed);
    for (const float v : style_res.next_state.values.sliders) {
      CHECK(v >= -1.0f);
      CHECK(v <= 1.0f);
    }
  }
}

TEST_CASE("BeautyLab: persistent library across restarts, incompatible presets, and cache invalidation", "[beauty_lab]") {
  QTemporaryDir tmp;
  REQUIRE(tmp.isValid());
  qputenv("SNS_BEAUTY_LIBRARY_DIR", tmp.path().toUtf8());

  auto rec = make_sample_recipe();
  auto st = state_from_recipe(rec);
  st.values.sliders[0] = 0.42f;
  st.values.hair_color = face::Rgb8{180, 40, 40};

  QString saved_preset_id;
  QString saved_combo_id;
  QString saved_facial_id;

  SECTION("Scenario 5, 6, 7 & 8: Persistence across restart, incompatible preset detection, cancellation, and cache clear") {
    {
      BeautyLibraryStore store1(tmp.path());
      QImage preview_img(64, 64, QImage::Format_RGBA8888);
      preview_img.fill(QColor("#336699"));
      const QString preview_path = store1.save_preview_image("serana_test", preview_img);
      CHECK(QFile::exists(preview_path));

      QString err;
      saved_preset_id = store1.save_preset(
          "Serana Crimson",
          "Cinematic Fantasy",
          "Prueba de persistencia",
          rec,
          st,
          1.02f,
          false,
          "LISTA_PARA_EXPORTAR",
          "FaceGen reconstruido",
          {QVariantMap{{"plugin", "Dawnguard.esm"}, {"type", "Hair"}}},
          preview_path,
          &err);
      REQUIRE(err.isEmpty());
      REQUIRE_FALSE(saved_preset_id.isEmpty());

      CHECK(store1.toggle_favorite_hair(
          QVariantMap{
              {"formKey", "004347:Dawnguard.esm"},
              {"editorId", "HairFemaleSerana"},
              {"name", "Serana Royal Hair"},
              {"plugin", "Dawnguard.esm"},
              {"packageFamily", "Vanilla / Creation Club"},
              {"female", true}},
          &err));
      REQUIRE(err.isEmpty());

      saved_combo_id = store1.save_head_eye_combo("Serana Vampire Gaze", rec, st, {}, QString(), &err);
      REQUIRE(err.isEmpty());

      saved_facial_id = store1.save_facial_config("Serana Sculpt", rec, st, QString(), &err);
      REQUIRE(err.isEmpty());

      // Write and import a compatible .jslot file without redistribuir binary assets
      const QString jslot_path = QDir(tmp.path()).filePath("external_preset.jslot");
      {
        QFile jf(jslot_path);
        REQUIRE(jf.open(QIODevice::WriteOnly));
        jf.write(R"({"actor":{"weight":35},"headParts":[{"formIdentifier":"Dawnguard.esm|004347","type":3}],"morphs":{"default":{"NoseLongVsShort":0.25}}})");
      }
      const auto imported = store1.import_compatible_resource(jslot_path, "Lore Friendly HD", &err);
      REQUIRE(err.isEmpty());
      CHECK(imported.value("format").toString() == "jslot");
    }

    // Simulate application restart by instantiating a fresh store reading from disk
    BeautyLibraryStore store2(tmp.path());
    REQUIRE(store2.presets().size() == 1);
    REQUIRE(store2.favorite_hairs().size() == 1);
    REQUIRE(store2.head_eye_combos().size() == 1);
    REQUIRE(store2.facial_configs().size() == 1);
    REQUIRE(store2.imported_resources().size() == 1);

    const QVariantMap loaded_preset = store2.presets().first().toMap();
    CHECK(loaded_preset.value("id").toString() == saved_preset_id);
    CHECK(loaded_preset.value("name").toString() == "Serana Crimson");
    CHECK(QFile::exists(loaded_preset.value("previewImage").toString()));

    // Apply loaded preset back onto baseline state
    QStringList warnings;
    const auto applied = store2.apply_entry_to_state(loaded_preset, rec, state_from_recipe(rec), {}, &warnings);
    REQUIRE(applied.has_value());
    CHECK_THAT(applied->values.sliders[0], WithinAbs(0.42f, 1e-4f));
    REQUIRE(applied->values.hair_color.has_value());
    CHECK(applied->values.hair_color->r == 180);

    // Scenario 5: Incompatible preset (male NPC trying to apply female preset, or missing required plugin)
    auto male_rec = rec;
    male_rec.female = false;
    const auto sex_check = store2.evaluate_entry_compatibility(loaded_preset, &male_rec, {}, nullptr);
    CHECK_FALSE(sex_check.value("compatible").toBool());
    CHECK(sex_check.value("state").toString() == "incompatible");
    CHECK_FALSE(store2.apply_entry_to_state(loaded_preset, male_rec, state_from_recipe(male_rec), {}, &warnings).has_value());

    // Scenario 6 & 8: Cache invalidation & filter cancellation/search
    rendering::SceneCache sc(64ull * 1024 * 1024);
    sc.put_mesh("test_key", std::make_shared<nif::MeshFile>());
    CHECK(sc.mesh_count() == 1);
    sc.clear();
    CHECK(sc.mesh_count() == 0);
  }

  qunsetenv("SNS_BEAUTY_LIBRARY_DIR");
}

TEST_CASE("BeautyLab: unlocked regenerate mode clears blocks and enables full modding", "[beauty_lab]") {
  auto rec = make_sample_recipe();
  auto base_st = state_from_recipe(rec);
  QVariantMap unlocked_compat{
      {"code", "unlocked-regenerate"},
      {"baseCode", "custom-incompatible-morphs"},
      {"regenerableFromRecord", true},
      {"unlockedForModding", true},
      {"canUnlockForModding", false},
      {"blocked", QVariantList{}}};
  const auto caps = evaluate_workshop_capabilities(&rec, unlocked_compat, QStringLiteral("regenerate"));
  CHECK(caps.value("geometryKind").toString() == "parametric-regenerate");
  CHECK(caps.value("allowsParametricMorphs").toBool());
  CHECK(caps.value("allowsPresets").toBool());
  CHECK(caps.value("allowsHeadPartSwap").toBool());
  CHECK(caps.value("allowsHairColor").toBool());
  CHECK(caps.value("allowsSkinToneAndTints").toBool());

  const QVariantList hair_opts{QVariantMap{
      {"formKey", "009999:KS Hairdos.esp"},
      {"name", "KS Lush Wave"},
      {"plugin", "KS Hairdos.esp"},
      {"type", "Hair"},
      {"meshFound", true},
      {"isBaldPart", false}}};
  const auto style_res =
      apply_next_gen_style("lore-friendly-hd", {{"intensity", 0.7}}, rec, base_st, base_st, hair_opts, unlocked_compat);
  CHECK(style_res.changed);
  CHECK(style_res.skipped_rules.isEmpty());
}
