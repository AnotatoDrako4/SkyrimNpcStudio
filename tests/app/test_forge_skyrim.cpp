#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>

#include "forge_blender_bridge.hpp"
#include "forge_skyrim_package.hpp"
#include "sns/creation/forge_project.hpp"
#include "sns/creation/skyrim_conversion.hpp"
#include "sns/textures/dds.hpp"
#include "temp_dir.hpp"

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

QString qs(const fs::path& path) { return QString::fromStdWString(path.wstring()); }
const fs::path kSkyrim = fs::path(SNS_TEST_MODELS_DIR) / "skyrim";

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

QByteArray sha(const fs::path& path) {
  return QCryptographicHash::hash(QByteArray::fromStdString(read(path)), QCryptographicHash::Sha256);
}

struct App {
  char name[22] = "sns_forge_asset_tests";
  char* args[2] = {name, nullptr};
  int count = 1;
  QCoreApplication app{count, args};
};

// A finished exportNif stage built from the committed fixture: the PyNifly NIF and a PNG where the job puts it.
fs::path make_stage(const fs::path& root, bool alpha = false) {
  const auto stage = root / "stage";
  fs::create_directories(stage / "Data/meshes/CharacterForge/goblet");
  fs::create_directories(stage / "Data/textures/CharacterForge/goblet");
  fs::copy_file(kSkyrim / "goblet.nif", stage / "Data/meshes/CharacterForge/goblet/goblet.nif");
  QImage image(64, 64, QImage::Format_RGBA8888);
  image.fill(QColor(200, 150, 60, alpha ? 100 : 255));
  REQUIRE(image.save(qs(stage / "Data/textures/CharacterForge/goblet/goblet_diffuse.png")));
  return stage;
}

fs::path make_project(const fs::path& root) {
  const auto project = root / "Prop.sncf";
  sns::creation::ForgeProjectStore::save(project, sns::creation::ForgeProjectStore::create("Prop"));
  return project;
}

Json report() { return Json::parse(read(kSkyrim / "goblet_report.json")); }
Json to_json(const QVariantMap& map) { return Json::parse(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson().toStdString()); }

void no_partial_folders(const fs::path& project) {
  if (!fs::exists(project / "exports/skyrim")) return;
  for (const auto& entry : fs::directory_iterator(project / "exports/skyrim"))
    CHECK(entry.path().filename().string().rfind(".partial-", 0) != 0);
}

}  // namespace

TEST_CASE("Forge Skyrim slug is a safe file stem", "[forge][skyrim]") {
  CHECK(sns::app::forge_skyrim::slug(QString::fromUtf8("Goblet de latón!")) == "goblet_de_lat_n");
  CHECK(sns::app::forge_skyrim::slug("..\\..\\evil") == "evil");
  CHECK(sns::app::forge_skyrim::slug("") == "model");
}

TEST_CASE("Forge Skyrim package writes a verified Type A resource inside the project", "[forge][skyrim]") {
  App app;
  sns::test::TempDir temp;
  const auto stage = make_stage(temp.path());
  const auto project = make_project(temp.path());
  const auto result = sns::app::forge_skyrim::package_export(qs(project), qs(stage), report(), "Goblet");
  INFO(to_json(result).dump(1));
  REQUIRE(result.value("packaged").toBool());
  CHECK(result.value("skyrimStatus").toString() == "PENDIENTE_SKYRIM");
  const fs::path folder(result.value("folder").toString().toStdWString());
  CHECK(fs::equivalent(folder.parent_path().parent_path().parent_path(), project));
  const auto nif = folder / "Data/meshes/CharacterForge/goblet/goblet.nif";
  REQUIRE(fs::is_regular_file(nif));
  CHECK(read(nif) == read(kSkyrim / "goblet.nif"));  // byte for byte
  const auto dds = folder / "Data/textures/characterforge/goblet/goblet_diffuse.dds";
  REQUIRE(fs::is_regular_file(dds));
  const auto image = sns::textures::parse_dds(read(dds));
  CHECK(image.width == 64);
  CHECK(image.format == sns::textures::TextureFormat::BC1);
  CHECK(image.mip_count == 7);
  const auto manifest = Json::parse(read(folder / "manifest.json"));
  CHECK(manifest.at("schema") == "sns.forge.skyrim.package/1");
  CHECK(manifest.at("skyrimStatus") == "PENDIENTE_SKYRIM");
  CHECK(manifest.at("plugin").is_null());
  for (const auto& file : manifest.at("files")) {
    const auto path = folder / file.at("path").get<std::string>();
    CHECK(file.at("bytes").get<std::size_t>() == fs::file_size(path));
    CHECK(file.at("sha256").get<std::string>() == sha(path).toHex().toUpper().toStdString());
  }
  no_partial_folders(project);
}

TEST_CASE("Forge Skyrim package keeps alpha as BC3", "[forge][skyrim]") {
  App app;
  sns::test::TempDir temp;
  const auto result = sns::app::forge_skyrim::package_export(qs(make_project(temp.path())), qs(make_stage(temp.path(), true)),
                                                             report(), "Goblet");
  REQUIRE(result.value("packaged").toBool());
  const fs::path folder(result.value("folder").toString().toStdWString());
  CHECK(sns::textures::parse_dds(read(folder / "Data/textures/characterforge/goblet/goblet_diffuse.dds")).format ==
        sns::textures::TextureFormat::BC3);
}

TEST_CASE("Forge Skyrim package writes nothing for a NIF that fails verification", "[forge][skyrim]") {
  App app;
  sns::test::TempDir temp;
  const auto project = make_project(temp.path());
  auto bad = report();
  bad["prepared"]["meshes"][0]["triangles"] = 10;  // the NIF no longer matches what Blender prepared
  const auto result = sns::app::forge_skyrim::package_export(qs(project), qs(make_stage(temp.path())), bad, "Goblet");
  CHECK_FALSE(result.value("packaged").toBool());
  CHECK_FALSE(result.value("verification").toMap().value("passed").toBool());
  CHECK((!fs::exists(project / "exports/skyrim") || fs::is_empty(project / "exports/skyrim")));
}

TEST_CASE("Forge Skyrim package refuses unsafe reports and projects", "[forge][skyrim]") {
  App app;
  sns::test::TempDir temp;
  const auto stage = make_stage(temp.path());
  const auto project = make_project(temp.path());
  SECTION("NIF path escaping the stage") {
    auto r = report();
    r["export"]["output"] = "Data\\meshes\\..\\..\\..\\evil.nif";
    CHECK_THROWS_AS(sns::app::forge_skyrim::package_export(qs(project), qs(stage), r, "Goblet"), std::invalid_argument);
  }
  SECTION("texture path outside Data\\textures") {
    auto r = report();
    r["prepare"]["textures"]["staged"][0]["nifPath"] = "C:\\Windows\\evil.dds";
    // The NIF names a texture the report no longer stages: verification refuses before any file is written.
    const auto result = sns::app::forge_skyrim::package_export(qs(project), qs(stage), r, "Goblet");
    CHECK_FALSE(result.value("packaged").toBool());
    CHECK((!fs::exists(project / "exports/skyrim") || fs::is_empty(project / "exports/skyrim")));
  }
  SECTION("missing staged texture interrupts the export") {
    fs::remove(stage / "Data/textures/CharacterForge/goblet/goblet_diffuse.png");
    CHECK_THROWS(sns::app::forge_skyrim::package_export(qs(project), qs(stage), report(), "Goblet"));
  }
  SECTION("not a .sncf project") {
    CHECK_THROWS_AS(sns::app::forge_skyrim::package_export(qs(temp.path()), qs(stage), report(), "Goblet"), std::invalid_argument);
  }
  no_partial_folders(project);  // a refused or interrupted package leaves nothing behind
}

TEST_CASE("Forge Skyrim mod request describes the verified package for the worker", "[forge][skyrim][mod]") {
  App app;
  sns::test::TempDir temp;
  const auto result = sns::app::forge_skyrim::package_export(qs(make_project(temp.path())), qs(make_stage(temp.path())), report(), "Goblet");
  REQUIRE(result.value("packaged").toBool());
  const auto folder = result.value("folder").toString();
  const auto request = sns::app::forge_skyrim::mod_request(folder, qs(temp.path() / "mods"));
  INFO(request.dump(1));
  CHECK(request.at("pluginName") == "CF_goblet.esp");
  CHECK(request.at("editorId") == "CF_goblet");
  CHECK(request.at("eslFlag") == true);
  CHECK(request.at("createZip") == true);
  CHECK(request.at("modelPath") == "CharacterForge\\goblet\\goblet.nif");
  CHECK(fs::equivalent(fs::path(QString::fromStdString(request.at("sourceDirectory").get<std::string>()).toStdWString()),
                       fs::path(folder.toStdWString())));
  // Only the game's files, with the manifest's hashes (the worker re-hashes them before copying).
  const auto manifest = Json::parse(read(fs::path(folder.toStdWString()) / "manifest.json"));
  REQUIRE(request.at("files").size() == 2);
  for (const auto& file : request.at("files")) {
    const auto relative = file.at("relativePath").get<std::string>();
    CHECK((relative.rfind("meshes/", 0) == 0 || relative.rfind("textures/", 0) == 0));
    bool listed = false;
    for (const auto& m : manifest.at("files"))
      listed |= m.at("path") == "Data/" + relative && m.at("sha256") == file.at("sha256");
    CHECK(listed);
  }
  // OBND: the nifly box of the goblet ([-4,-4,-2.8]..[4,4,16.8]) rounded outwards to whole game units.
  const auto nif = to_json(result.value("verification").toMap());
  for (int i = 0; i < 3; ++i) {
    CHECK(request.at("boundsMin").at(i).get<int>() <= nif.at("boundsMin").at(i).get<double>());
    CHECK(request.at("boundsMax").at(i).get<int>() >= nif.at("boundsMax").at(i).get<double>());
    CHECK(request.at("boundsMax").at(i).get<int>() - nif.at("boundsMax").at(i).get<double>() < 1.0);
  }
  CHECK(request.at("boundsMin") == Json::array({-4, -4, -3}));
  CHECK(request.at("boundsMax") == Json::array({4, 4, 17}));
}

TEST_CASE("Forge Skyrim mod request refuses folders it cannot vouch for", "[forge][skyrim][mod]") {
  App app;
  sns::test::TempDir temp;
  const auto result = sns::app::forge_skyrim::package_export(qs(make_project(temp.path())), qs(make_stage(temp.path())), report(), "Goblet");
  REQUIRE(result.value("packaged").toBool());
  const fs::path folder(result.value("folder").toString().toStdWString());
  const auto out = qs(temp.path() / "mods");
  const auto rewrite = [&](const char* name, const std::function<void(Json&)>& change) {
    auto json = Json::parse(read(folder / name));
    change(json);
    std::ofstream(folder / name, std::ios::binary | std::ios::trunc) << json.dump(1);
  };
  SECTION("not a package") { CHECK_THROWS_AS(sns::app::forge_skyrim::mod_request(qs(temp.path()), out), std::runtime_error); }
  SECTION("NIF not verified") {
    rewrite("manifest.json", [](Json& m) { m["nifVerified"] = false; });
    CHECK_THROWS_AS(sns::app::forge_skyrim::mod_request(qs(folder), out), std::invalid_argument);
  }
  SECTION("verification without bounds (older export)") {
    rewrite("verification.json", [](Json& v) { v["nif"].erase("boundsMin"); });
    CHECK_THROWS_AS(sns::app::forge_skyrim::mod_request(qs(folder), out), std::invalid_argument);
  }
  SECTION("bounds that do not fit a 16-bit OBND") {
    rewrite("verification.json", [](Json& v) { v["nif"]["boundsMax"][2] = 40000.0; });
    CHECK_THROWS_AS(sns::app::forge_skyrim::mod_request(qs(folder), out), std::invalid_argument);
  }
  SECTION("a second NIF") {
    rewrite("manifest.json", [](Json& m) { m["files"].push_back({{"path", "Data/meshes/other.nif"}, {"sha256", std::string(64, 'A')}}); });
    CHECK_THROWS_AS(sns::app::forge_skyrim::mod_request(qs(folder), out), std::invalid_argument);
  }
  SECTION("no NIF") {
    rewrite("manifest.json", [](Json& m) {
      Json kept = Json::array();
      for (const auto& f : m["files"]) if (f["path"].get<std::string>().find(".nif") == std::string::npos) kept.push_back(f);
      m["files"] = kept;
    });
    CHECK_THROWS_AS(sns::app::forge_skyrim::mod_request(qs(folder), out), std::invalid_argument);
  }
}

TEST_CASE("Blender Skyrim job rejects unsafe arguments before starting Blender", "[forge][skyrim][blender]") {
  App app;
  sns::app::ForgeBlenderBridge bridge;
  const auto source = qs(fs::path(SNS_TEST_MODELS_DIR) / "triangle.obj");
  const auto program = QCoreApplication::applicationFilePath();
  CHECK_THROWS_AS(bridge.runSkyrimJob(program, "deleteEverything", source, "CharacterForge\\x", "x.nif"), std::invalid_argument);
  CHECK_THROWS_AS(bridge.runSkyrimJob(program, "exportNif", source, "..\\..\\Windows", "x.nif"), std::invalid_argument);
  CHECK_THROWS_AS(bridge.runSkyrimJob(program, "exportNif", source, "CharacterForge\\x", "a/b.nif"), std::invalid_argument);
  CHECK_THROWS_AS(bridge.runSkyrimJob(program, "exportNif", source, "CharacterForge\\x", "x.nif\" --python-expr \"x"),
                  std::invalid_argument);
  CHECK_THROWS_AS(bridge.runSkyrimJob("missing-blender.exe", "inspect", source, "CharacterForge\\x", "x.nif"), std::invalid_argument);
  CHECK_FALSE(bridge.busy());
}

namespace {
// Runs one real Blender job and waits for its end; the stage is copied before the bridge deletes it.
struct JobRun {
  QString failure, operation;
  QVariantMap report;
  QStringList progress;
  sns::test::TempDir kept;
  fs::path stage;
};
void run_job(sns::app::ForgeBlenderBridge& bridge, const QString& executable, const QString& operation, const QString& source,
             const QString& name, JobRun& run, int cancel_after_ms = -1, const QString& profile = QStringLiteral("static")) {
  QEventLoop loop;
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] { run.failure = "watchdog"; bridge.cancel(); loop.quit(); });
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::failed, &loop, [&](const QString& m) { run.failure = m; loop.quit(); });
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::progressed, &loop, [&](const QString& step, double) { run.progress << step; });
  QObject::connect(&bridge, &sns::app::ForgeBlenderBridge::skyrimCompleted, &loop,
                   [&](const QString& op, const QString& stage, const QVariantMap& report) {
    run.operation = op;
    run.report = report;
    run.stage = run.kept.path() / "stage";
    fs::copy(fs::path(stage.toStdWString()), run.stage, fs::copy_options::recursive);
    loop.quit();
  });
  bridge.runSkyrimJob(executable, operation, source, "CharacterForge\\" + name, name + ".nif", profile);
  if (cancel_after_ms >= 0) QTimer::singleShot(cancel_after_ms, &bridge, [&] { bridge.cancel(); });
  watchdog.start(180000);
  loop.exec();
}

bool local_pipeline(QString& executable, QString& fixtures) {
  executable = qEnvironmentVariable("SNS_LOCAL_BLENDER");
  fixtures = qEnvironmentVariable("SNS_LOCAL_SKYRIM_FIXTURES");  // make_test_models.py -- <dir> skyrim_prop
  return !executable.isEmpty() && QFileInfo(executable).isExecutable() && !fixtures.isEmpty();
}
}  // namespace

TEST_CASE("Blender Skyrim job can be cancelled", "[forge][skyrim][blender][local]") {
  QString executable, fixtures;
  if (!local_pipeline(executable, fixtures)) SKIP("Set SNS_LOCAL_BLENDER and SNS_LOCAL_SKYRIM_FIXTURES for the real Blender job tests");
  App app;
  sns::app::ForgeBlenderBridge bridge;
  JobRun run;
  run_job(bridge, executable, "exportNif", fixtures + "/skyrim_prop.blend", "goblet", run, 300);
  CHECK(run.operation.isEmpty());
  CHECK(run.failure.contains(QStringLiteral("cancelada")));
  CHECK_FALSE(bridge.busy());
}

TEST_CASE("Blender to Character Forge to verified NIF, end to end", "[forge][skyrim][blender][local]") {
  QString executable, fixtures;
  if (!local_pipeline(executable, fixtures))
    SKIP("Set SNS_LOCAL_BLENDER and SNS_LOCAL_SKYRIM_FIXTURES (with PyNifly installed) for the real NIF pipeline");
  App app;
  const auto source = fixtures + "/skyrim_prop.blend";
  REQUIRE(QFileInfo(source).isFile());
  const auto source_sha = sha(fs::path(source.toStdWString()));
  sns::test::TempDir temp;
  const auto project = make_project(temp.path());
  sns::app::ForgeBlenderBridge bridge;

  JobRun inspect;
  run_job(bridge, executable, "inspect", source, "goblet", inspect);
  INFO(inspect.failure.toStdString());
  REQUIRE(inspect.failure.isEmpty());
  const auto inspected = to_json(inspect.report);
  CHECK(sns::creation::skyrim::classify(inspected).at("exportable") == true);
  CHECK(inspected.at("pynifly").at("available") == true);
  CHECK(inspected.at("inspection").at("totals").at("triangles") == 92);
  CHECK_FALSE(inspected.contains("export"));  // inspection never writes a NIF

  JobRun exported;
  run_job(bridge, executable, "exportNif", source, "goblet", exported);
  INFO(exported.failure.toStdString());
  REQUIRE(exported.failure.isEmpty());
  CHECK(exported.progress.contains("export"));
  CHECK(exported.progress.contains("readback"));
  const auto result = sns::app::forge_skyrim::package_export(qs(project), qs(exported.stage), to_json(exported.report), "goblet");
  INFO(to_json(result).dump(1));
  REQUIRE(result.value("packaged").toBool());
  CHECK(result.value("verification").toMap().value("passed").toBool());
  CHECK(sha(fs::path(source.toStdWString())) == source_sha);  // the original model is never written
}

TEST_CASE("Blender NIF pipeline omits a missing texture instead of writing its path", "[forge][skyrim][blender][local]") {
  QString executable, fixtures;
  if (!local_pipeline(executable, fixtures)) SKIP("Set SNS_LOCAL_BLENDER and SNS_LOCAL_SKYRIM_FIXTURES for the real NIF pipeline");
  App app;
  sns::test::TempDir temp;
  sns::app::ForgeBlenderBridge bridge;
  JobRun exported;
  run_job(bridge, executable, "exportNif", fixtures + "/skyrim_prop_missing_texture.blend", "goblet_missing", exported);
  INFO(exported.failure.toStdString());
  REQUIRE(exported.failure.isEmpty());
  const auto report = to_json(exported.report);
  CHECK(report.at("prepare").at("textures").at("missing").size() == 1);
  bool warned = false;
  const auto classification = sns::creation::skyrim::classify(report);  // named: a range-for over a temporary's member dangles
  for (const auto& issue : classification.at("issues")) warned |= issue.at("code") == "textureMissing";
  CHECK(warned);
  const auto result =
      sns::app::forge_skyrim::package_export(qs(make_project(temp.path())), qs(exported.stage), report, "goblet_missing");
  INFO(to_json(result).dump(1));
  REQUIRE(result.value("packaged").toBool());
  for (const auto& shape : result.value("verification").toMap().value("shapes").toList())
    CHECK(shape.toMap().value("textures").toMap().isEmpty());
}

// --- SkinnedMesh (Character Forge 3.0) ---------------------------------------------------------------------------------
// Fixtures: make_test_models.py -- <dir> skyrim_skinned (local only: its armature is PyNifly's import of the Skyrim
// reference skeleton). The reference is read only, from SNS_FORGE_REFERENCE_SKELETON or the PyNifly add-on.
namespace {
bool local_skinned(QString& executable, QString& fixtures) {
  executable = qEnvironmentVariable("SNS_LOCAL_BLENDER");
  fixtures = qEnvironmentVariable("SNS_LOCAL_SKINNED_FIXTURES");
  return !executable.isEmpty() && QFileInfo(executable).isExecutable() && QFileInfo(fixtures + "/skyrim_skinned.blend").isFile();
}
Json mapped(const JobRun& run) {
  auto json = to_json(run.report);
  sns::app::forge_skyrim::attach_skeleton_mapping(json, sns::app::forge_skyrim::default_reference_skeleton(json));
  return json;
}
QStringList check_codes(const QVariantMap& verification, bool passed) {
  QStringList out;
  for (const auto& c : verification.value("checks").toList())
    if (c.toMap().value("passed").toBool() == passed) out << c.toMap().value("code").toString();
  return out;
}
}  // namespace

TEST_CASE("Blender skinned mesh to verified skinned NIF, end to end", "[forge][skyrim][blender][skinned][local]") {
  QString executable, fixtures;
  if (!local_skinned(executable, fixtures)) SKIP("Set SNS_LOCAL_BLENDER and SNS_LOCAL_SKINNED_FIXTURES (make_test_models.py skyrim_skinned)");
  App app;
  const auto source = fixtures + "/skyrim_skinned.blend";
  const auto source_sha = sha(fs::path(source.toStdWString()));
  sns::test::TempDir temp;
  const auto project = make_project(temp.path());
  sns::app::ForgeBlenderBridge bridge;

  JobRun inspect;
  run_job(bridge, executable, "inspect", source, "skinned_tube", inspect, -1, "skinned");
  INFO(inspect.failure.toStdString());
  REQUIRE(inspect.failure.isEmpty());
  const auto inspected = mapped(inspect);
  REQUIRE(inspected.contains("skeletonMapping"));
  const auto& mapping = inspected.at("skeletonMapping");
  INFO(mapping.at("weighted").dump());
  CHECK(mapping.at("allWeightedVerified") == true);  // 4 spine bones: same name, parent and bind as the reference
  CHECK(mapping.at("weighted").at("verified") == 4);
  const auto classification = sns::creation::skyrim::classify(inspected, "skinned");
  INFO(classification.dump(1));
  CHECK(classification.at("exportable") == true);
  CHECK(sns::creation::skyrim::skinned_states(inspected, classification, nullptr).at("current") == "SKIN_VALIDATED");
  CHECK(sns::creation::skyrim::classify(inspected, "static").at("exportable") == false);  // the static rules still hold

  JobRun exported;
  run_job(bridge, executable, "exportNif", source, "skinned_tube", exported, -1, "skinned");
  INFO(exported.failure.toStdString());
  REQUIRE(exported.failure.isEmpty());
  const auto result = sns::app::forge_skyrim::package_export(qs(project), qs(exported.stage), mapped(exported), "skinned_tube");
  const auto verification = result.value("verification").toMap();
  INFO(to_json(verification).dump(1));
  CHECK(check_codes(verification, false).isEmpty());
  for (const auto* code : {"skinned", "bonesMatchWeights", "influencesValid", "skinPartitions", "bindMatchesBlender",
                           "bindMatchesReference", "weightsMatchBlender", "triangles", "bounds", "geometry", "pyniflyAgrees"})
    CHECK(check_codes(verification, true).contains(QString::fromLatin1(code)));
  REQUIRE(result.value("packaged").toBool());
  CHECK(result.value("states").toMap().value("current").toString() == "PENDIENTE_SKYRIM");
  CHECK(result.value("manifest").toMap().value("kind").toString() == "SkinnedMesh-NIF");
  const auto shape = verification.value("shapes").toList().value(0).toMap();
  CHECK(shape.value("bones").toInt() == 4);
  CHECK(shape.value("triangles").toInt() == 216);
  if (const auto evidence = qEnvironmentVariable("SNS_FORGE_EVIDENCE_DIR"); !evidence.isEmpty()) {  // keep the package for review
    const auto target = fs::path(evidence.toStdWString()) / "skinned-package";
    fs::remove_all(target);
    fs::copy(fs::path(result.value("folder").toString().toStdWString()), target, fs::copy_options::recursive);
    std::ofstream(target / "result.json") << to_json(result).dump(1);
  }
  // The skinned package never becomes a STAT mod.
  CHECK_THROWS_AS(sns::app::forge_skyrim::mod_request(result.value("folder").toString(), qs(temp.path() / "mods")), std::invalid_argument);
  CHECK(sha(fs::path(source.toStdWString())) == source_sha);
}

// Diagnosis of a user's model (read only): SNS_FORGE_DIAGNOSE_MODEL=<.blend>, SNS_FORGE_EVIDENCE_DIR=<folder>. Writes
// <folder>/diagnosis.json (classification, mapping, states). Asserts only what must hold for any model: the job ran,
// nothing was exported and the original is unchanged; the verdict is the report's, not the test's.
TEST_CASE("Skinned diagnosis of a user model", "[forge][skyrim][blender][skinned][diagnose][local]") {
  const auto executable = qEnvironmentVariable("SNS_LOCAL_BLENDER");
  const auto model = qEnvironmentVariable("SNS_FORGE_DIAGNOSE_MODEL");
  const auto evidence = qEnvironmentVariable("SNS_FORGE_EVIDENCE_DIR");
  if (executable.isEmpty() || model.isEmpty() || evidence.isEmpty()) SKIP("Set SNS_LOCAL_BLENDER, SNS_FORGE_DIAGNOSE_MODEL and SNS_FORGE_EVIDENCE_DIR");
  App app;
  const auto before = sha(fs::path(model.toStdWString()));
  sns::app::ForgeBlenderBridge bridge;
  JobRun run;
  run_job(bridge, executable, "inspect", model, "diagnosis", run, -1, "skinned");
  INFO(run.failure.toStdString());
  REQUIRE(run.failure.isEmpty());
  const auto report = mapped(run);
  const auto classification = sns::creation::skyrim::classify(report, "skinned");
  auto inspection = report.at("inspection");
  for (auto& mesh : inspection.at("meshes"))
    if (mesh.contains("skin")) mesh["skin"].erase("weightSamples");  // keep the evidence readable
  for (auto& armature : inspection.at("armatures")) armature.erase("bonesDetail");
  std::ofstream(fs::path(evidence.toStdWString()) / "diagnosis.json")
      << Json{{"model", model.toStdString()}, {"sourceSha256", report.value("sourceSha256", "")}, {"classification", classification},
              {"skeletonMapping", report.value("skeletonMapping", Json::object())},
              {"states", sns::creation::skyrim::skinned_states(report, classification, nullptr)}, {"inspection", inspection}}
             .dump(1);
  CHECK_FALSE(report.contains("export"));
  CHECK(sha(fs::path(model.toStdWString())) == before);
}

TEST_CASE("Skinned pipeline blocks unweighted vertices and bones the reference lacks", "[forge][skyrim][blender][skinned][local]") {
  QString executable, fixtures;
  if (!local_skinned(executable, fixtures)) SKIP("Set SNS_LOCAL_BLENDER and SNS_LOCAL_SKINNED_FIXTURES (make_test_models.py skyrim_skinned)");
  App app;
  sns::app::ForgeBlenderBridge bridge;
  const auto issue = [](const Json& classification, const std::string& code) {
    for (const auto& i : classification.at("issues"))
      if (i.at("code") == code) return i.at("severity").get<std::string>();
    return std::string{};
  };
  JobRun unweighted;
  run_job(bridge, executable, "inspect", fixtures + "/skyrim_skinned_unweighted.blend", "unweighted", unweighted, -1, "skinned");
  REQUIRE(unweighted.failure.isEmpty());
  const auto a = sns::creation::skyrim::classify(mapped(unweighted), "skinned");
  CHECK(issue(a, "unweightedVertices") == "blocking");
  CHECK(a.at("exportable") == false);

  JobRun custom;
  run_job(bridge, executable, "inspect", fixtures + "/skyrim_skinned_custom_bone.blend", "custom_bone", custom, -1, "skinned");
  REQUIRE(custom.failure.isEmpty());
  const auto report = mapped(custom);
  const auto b = sns::creation::skyrim::classify(report, "skinned");
  CHECK(issue(b, "boneUnrecognized") == "blocking");
  CHECK(sns::creation::skyrim::skinned_states(report, b, nullptr).at("current") == "RIG_INSPECTED");
}
