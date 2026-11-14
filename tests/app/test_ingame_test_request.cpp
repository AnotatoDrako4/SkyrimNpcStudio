// M2 step 4: what StudioController::prepareInGameTest asks the worker to package, decided without the worker.
#include <catch2/catch_test_macros.hpp>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>

#include "ingame_test_request.hpp"

using namespace sns;
using namespace sns::app;

namespace {

const QByteArray kPluginBytes = QByteArrayLiteral("TES4 synthetic plugin bytes for the in-game test kit");

QString sha_of(const QByteArray& bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()).toUpper();
}

void write(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

QByteArray manifest_json(const QString& sha) {
  return QStringLiteral(R"({"format":"skyrim-npc-studio/export","mode":"forge-static-resource",
"plugin":{"fileName":"CF_prop.esp","sha256":"%1","eslFlagged":true},
"records":[{"formKey":"000800:CF_prop.esp","recordType":"STAT","editorId":"CF_prop","kind":"new"}]})")
      .arg(sha)
      .toUtf8();
}

mo2::ResolvedPlugin active_plugin(const std::string& name, const QString& file, bool light) {
  mo2::ResolvedPlugin p;
  p.name = name;
  p.active = true;
  p.path = file.toStdWString();
  p.provider = "Mod of " + name;
  mo2::PluginHeader h;
  h.is_light = light;
  h.is_master = true;
  p.header = h;
  return p;
}

struct Fixture {
  QTemporaryDir root;
  QString exports;     // <root>/exports  (where the studio wrote the export)
  QString export_dir;  // <root>/exports/CF_prop
  QString installed;   // <root>/mods/CF_prop/CF_prop.esp (what the profile loads)
  mo2::EffectiveLoadOrder load_order;
  QDateTime now = QDateTime(QDate(2026, 11, 5), QTime(9, 30, 15), QTimeZone::UTC);

  explicit Fixture(const QByteArray& installed_bytes = kPluginBytes) {
    REQUIRE(root.isValid());
    exports = root.filePath(QStringLiteral("exports"));
    export_dir = exports + QStringLiteral("/CF_prop");
    write(export_dir + QStringLiteral("/manifest.json"), manifest_json(sha_of(kPluginBytes)));
    installed = root.filePath(QStringLiteral("mods/CF_prop/CF_prop.esp"));
    write(installed, installed_bytes);
    load_order.active.push_back(active_plugin("Skyrim.esm", root.filePath(QStringLiteral("Skyrim.esm")), false));
    load_order.active.push_back(active_plugin("CF_prop.esp", installed, true));
  }

  InGameTestPlan plan(const QString& input, std::vector<ForbiddenRoot> forbidden = {}) const {
    return plan_ingame_test(input, QStringLiteral("Pruebas SNS"), load_order, forbidden, now);
  }
};

}  // namespace

TEST_CASE("An export the profile loads byte for byte becomes an in-game test kit request", "[app][ingame-test]") {
  const Fixture fx;
  const auto plan = fx.plan(fx.export_dir);
  INFO(plan.errors.join(QStringLiteral(" | ")).toStdString());
  REQUIRE(plan.ok());

  CHECK(plan.installed_sha256 == sha_of(kPluginBytes));
  CHECK(plan.kit.ok);
  CHECK(plan.kit.batch_file == "SNS_Test_CF_prop.txt");
  CHECK(plan.mod_name == QStringLiteral("SNS_Test_CF_prop_20261105-093015"));
  // The kit goes next to the export it tests, never inside it.
  CHECK(QDir::cleanPath(plan.output_directory) == QDir::cleanPath(fx.exports));

  const auto& p = plan.params;
  CHECK(p.at("kind") == "ingame-test-kit");
  CHECK(p.at("modName") == "SNS_Test_CF_prop_20261105-093015");
  CHECK(p.at("createZip") == true);
  REQUIRE(p.at("files").size() == 2);
  CHECK(p.at("files")[0].at("relativePath") == "SNS_Test_CF_prop.txt");
  CHECK(p.at("files")[0].at("content") == plan.kit.batch_text);
  CHECK(p.at("files")[1].at("relativePath") == "ACTA_SNS_Test_CF_prop.md");
  CHECK(p.at("metadata").at("inGameVerification") == "PENDIENTE_SKYRIM");
  CHECK(p.at("metadata").at("profile") == "Pruebas SNS");
  // FE + light index 000 + local id 800: the line the player types after "bat".
  CHECK(plan.kit.batch_text.find("player.placeatme FE000800 1") != std::string::npos);
}

TEST_CASE("The manifest itself can be chosen instead of its folder", "[app][ingame-test]") {
  const Fixture fx;
  const auto plan = fx.plan(fx.export_dir + QStringLiteral("/manifest.json"));
  INFO(plan.errors.join(QStringLiteral(" | ")).toStdString());
  CHECK(plan.ok());
  CHECK(QDir::cleanPath(plan.export_folder) == QDir::cleanPath(fx.export_dir));
}

TEST_CASE("A plugin with other bytes than the export is not packaged", "[app][ingame-test]") {
  const Fixture fx(QByteArrayLiteral("an older build with the same name"));
  const auto plan = fx.plan(fx.export_dir);
  CHECK_FALSE(plan.ok());
  CHECK(plan.params.is_null());
  CHECK(plan.errors.join(' ').contains(QStringLiteral("SHA-256")));
}

TEST_CASE("Folders without an export manifest are refused with a reason", "[app][ingame-test]") {
  const Fixture fx;
  CHECK_FALSE(fx.plan(fx.root.filePath(QStringLiteral("nowhere"))).ok());
  CHECK_FALSE(fx.plan(QString()).ok());

  write(fx.root.filePath(QStringLiteral("other/manifest.json")), QByteArrayLiteral(R"({"format":"something-else"})"));
  const auto foreign = fx.plan(fx.root.filePath(QStringLiteral("other")));
  CHECK_FALSE(foreign.ok());
  CHECK(foreign.params.is_null());

  write(fx.root.filePath(QStringLiteral("big/manifest.json")), QByteArray(kMaxExportManifestBytes + 1, ' '));
  CHECK_FALSE(fx.plan(fx.root.filePath(QStringLiteral("big"))).ok());
}

TEST_CASE("An export kept inside MO2 or the game gets no kit beside it", "[app][ingame-test]") {
  const Fixture fx;
  const auto plan = fx.plan(fx.export_dir, {{QStringLiteral("MO2 mods"), fx.exports.toStdWString()}});
  CHECK_FALSE(plan.ok());
  CHECK(plan.params.is_null());
}
