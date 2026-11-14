// Stand-in for blender.exe in the ForgeBlenderBridge process tests. It answers --version like Blender and, for a run,
// writes what forge_bridge.py / forge_skyrim.py would write, then ends the way SNS_FAKE_BLENDER_SCENARIO says:
//   ok                 GLB/NIF + report, OK marker, exit 0
//   ok-then-crash      same, then "Writing: <temp>\fake.crash.txt" and an access violation exit (0xC0000005)
//   ok-then-nonzero    same, then exit 3
//   ok-then-hang       same, then never exits
//   hang               never exits, writes nothing
//   crash-before       progress lines, then 0xC0000005 without a report
//   truncated          report for the whole GLB, but the file on disk is cut in half; exit 0
//   hash-mismatch      report with a wrong SHA-256; exit 0
//   error-report       report status "error", ERROR marker, exit 17
//   no-marker          GLB + report, no marker, exit 0
//   badversion         --version does not look like Blender
// SNS_FAKE_BLENDER_GLB: GLB copied as the output (default: a minimal valid GLB). SNS_FAKE_BLENDER_TEMPDIR=1 creates a
// "<temp>/blender_fakeNNNN" folder and reports it as blenderTempDir, like Blender's own session folder.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
void out(const QByteArray& line) {
  std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

[[noreturn]] void crash() {
#ifdef _WIN32
  // The exit code a real access violation leaves, without a Windows Error Reporting dialog.
  TerminateProcess(GetCurrentProcess(), 0xC0000005u);
#endif
  std::abort();
}

[[noreturn]] void hang() {
  for (;;) QThread::sleep(3600);
}

QByteArray minimal_glb() {
  QByteArray json = R"({"asset":{"version":"2.0","generator":"sns_fake_blender"},"scene":0,"scenes":[{"nodes":[]}]})";
  while (json.size() % 4) json += ' ';
  QByteArray glb;
  const auto le32 = [&](quint32 v) { for (int i = 0; i < 4; ++i) glb += char((v >> (8 * i)) & 0xFF); };
  glb += "glTF";
  le32(2);
  le32(static_cast<quint32>(12 + 8 + json.size()));
  le32(static_cast<quint32>(json.size()));
  glb += "JSON";
  glb += json;
  return glb;
}

bool write(const QString& path, const QByteArray& bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QString sha(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto scenario = qEnvironmentVariable("SNS_FAKE_BLENDER_SCENARIO", "ok");
  const auto args = app.arguments();
  if (args.contains("--version")) {
    out(scenario == "badversion" ? "NotBlender 1.0" : "Blender 5.2.3 LTS (sns_fake_blender)");
    return 0;
  }
  const auto dash = args.indexOf("--");
  const auto rest = dash >= 0 ? args.mid(dash + 1) : QStringList();
  if (rest.size() != 1 && rest.size() != 3) { out("fake blender: expected '-- SOURCE OUTPUT REPORT' or '-- JOB'"); return 2; }

  QString temp_dir;
  if (qEnvironmentVariableIntValue("SNS_FAKE_BLENDER_TEMPDIR") == 1) {  // like forge_resources.announce_tempdir()
    temp_dir = QDir::temp().filePath(QStringLiteral("blender_fake%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(temp_dir);
    write(temp_dir + "/session.tmp", "x");
    out("SNS_FORGE_TEMPDIR " + QDir::toNativeSeparators(temp_dir).toUtf8() + "\\");
  }
  out(R"(SNS_FORGE_PROGRESS {"step": "open", "fraction": 0.05})");
  if (scenario == "hang") hang();
  if (scenario == "crash-before") crash();

  QJsonObject report;
  QString report_path;
  const QByteArray marker_ok = rest.size() == 3 ? "SNS_FORGE_BRIDGE_OK" : "SNS_FORGE_SKYRIM_OK";
  const QByteArray marker_error = rest.size() == 3 ? "SNS_FORGE_BRIDGE_ERROR" : "SNS_FORGE_SKYRIM_ERROR fake failure";
  if (rest.size() == 3) {  // forge_bridge.py: SOURCE OUTPUT.glb REPORT.json
    report_path = rest[2];
    QByteArray glb = minimal_glb();
    if (const auto source = qEnvironmentVariable("SNS_FAKE_BLENDER_GLB"); !source.isEmpty()) {
      QFile file(source);
      if (!file.open(QIODevice::ReadOnly)) return 4;
      glb = file.readAll();
    }
    report = {{"schema", 1}, {"blenderVersion", "5.2.3 LTS"}, {"sourceFormat", QFileInfo(rest[0]).suffix().toLower()},
              {"status", "ok"}, {"meshes", 1}, {"outputBytes", glb.size()}, {"outputSha256", sha(glb)}};
    if (scenario == "hash-mismatch") report["outputSha256"] = QString(64, '0');
    if (!write(rest[1], scenario == "truncated" ? glb.left(glb.size() / 2) : glb)) return 5;
  } else {  // forge_skyrim.py: JOB.json
    QFile file(rest[0]);
    if (!file.open(QIODevice::ReadOnly)) return 6;
    const auto job = QJsonDocument::fromJson(file.readAll()).object();
    report_path = job.value("report").toString();
    report = {{"schema", "sns.forge.skyrim/1"}, {"operation", job.value("operation")}, {"profile", job.value("profile")},
              {"status", "ok"}, {"blenderVersion", "5.2.3 LTS"}};
    if (job.value("operation").toString() == "exportNif") {
      const QByteArray nif = "Gamebryo File Format, Version 20.2.0.7\n" + QByteArray(256, '\x01');
      const auto relative = "Data/meshes/" + job.value("texturePrefix").toString().replace('\\', '/') + "/" + job.value("nifName").toString();
      if (!write(job.value("stage").toString() + "/" + relative, scenario == "truncated" ? nif.left(40) : nif)) return 7;
      report["export"] = QJsonObject{{"output", relative}, {"bytes", nif.size()},
                                     {"sha256", scenario == "hash-mismatch" ? QString(64, '0') : sha(nif).toUpper()}};
    }
  }
  if (!temp_dir.isEmpty()) report["blenderTempDir"] = QDir::toNativeSeparators(temp_dir) + "\\";
  if (scenario == "error-report") {
    report["status"] = "error";
    report["message"] = "Fake failure: the model has no mesh objects";
  }
  if (!write(report_path, QJsonDocument(report).toJson())) return 8;
  out(R"(SNS_FORGE_PROGRESS {"step": "export", "fraction": 0.9})");
  if (scenario == "error-report") {
    out(marker_error);
    std::fputs("Traceback (most recent call last):\nValueError: fake failure\n", stderr);
    return 17;
  }
  if (scenario != "no-marker") out(marker_ok);
  if (scenario == "ok-then-crash") {
    std::fputs("Error   : EXCEPTION_ACCESS_VIOLATION\n", stderr);
    std::fflush(stderr);
    out("Writing: " + QDir::toNativeSeparators(QDir::temp().filePath("fake.crash.txt")).toUtf8());
    crash();
  }
  if (scenario == "ok-then-nonzero") return 3;
  if (scenario == "ok-then-hang") hang();
  return 0;
}
