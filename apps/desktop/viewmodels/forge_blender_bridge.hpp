#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <memory>

namespace sns::app {

// What one Blender run produced, told apart. A marker on stdout, a report that says "ok", a file on disk and a clean
// process exit are different facts: Blender 5.2 was seen writing a valid GLB and SNS_FORGE_BRIDGE_OK and then dying
// with 0xC0000005 while tearing the scene down. None of the facts alone means the result can be used.
struct BlenderRunFacts {
  // How the process ended. hungAfterResult: it printed the OK marker and then did not exit within the grace period.
  enum class Exit { normal, nonZero, crashed, timedOut, hungAfterResult, cancelled, failedToStart };
  Exit exit = Exit::normal;
  qint64 exit_code = 0;
  qint64 elapsed_ms = 0;
  bool marker_ok = false;     // the script printed its SNS_FORGE_*_OK line
  bool marker_error = false;  // the script printed its SNS_FORGE_*_ERROR line
  QString report = QStringLiteral("missing");    // the script's report: ok | error | missing | invalid
  QString report_message;                        // the script's own error text
  QString artifact = QStringLiteral("missing");  // verified | missing | truncated | hashMismatch | invalid | notApplicable
  QString artifact_detail;
  QString crash_log;   // the blender.crash.txt Blender said it wrote
  QString stderr_tail; // last lines of stderr, for the technical details
};

enum class BlenderVerdict { accepted, recoverable, failed };

// accepted: exit code 0, OK marker, report "ok", artifact verified.
// recoverable: the same evidence of a finished, verified result, but the process then crashed, returned non-zero or hung
//   on exit. The result may be used only after an explicit decision, and it keeps a visible warning.
// failed: anything else. A cancelled run or one that never started is never recoverable.
BlenderVerdict judge_blender_run(const BlenderRunFacts& facts);
QString exit_name(BlenderRunFacts::Exit exit);
// What happened and what to do, in Spanish, for the person using the studio.
QString describe_blender_run(const BlenderRunFacts& facts, BlenderVerdict verdict);
// {verdict, exit, exitCode, exitHex, exitMeaning, seconds, markerOk, markerError, report, reportMessage, artifact,
//  artifactDetail, crashLog, stderrTail, message}
QVariantMap to_variant(const BlenderRunFacts& facts, BlenderVerdict verdict);

// Runs only the resource-embedded Blender scripts. The caller validates/imports their outputs before use.
class ForgeBlenderBridge final : public QObject {
  Q_OBJECT
 public:
  explicit ForgeBlenderBridge(QObject* parent = nullptr);
  ~ForgeBlenderBridge() override;
  bool busy() const { return phase_ == Phase::version || phase_ == Phase::conversion; }
  // A finished, verified result of a run that ended badly is waiting for acceptRecovered() or discardRecovered().
  bool hasPendingRecovery() const { return phase_ == Phase::awaitingDecision; }
  // The facts of the last run (to_variant), plus decision: accepted | recovered | discarded when one was taken.
  QVariantMap lastRun() const { return last_run_; }
  void convert(const QString& executable, const QString& source);
  // Runs tools/blender/forge_skyrim.py (contract sns.forge.skyrim/1): operation "inspect" or "exportNif" on a
  // copy of `source` inside a fresh staging folder. Blender receives only the path of a job file the bridge
  // writes; never Python text or a shell command. texture_prefix is "Word\word" (Data-relative folder under
  // meshes\ and textures\), nif_name "<slug>.nif". profile: "static" (Type A) or "skinned" (SkinnedMesh).
  void runSkyrimJob(const QString& executable, const QString& operation, const QString& source,
                    const QString& texture_prefix, const QString& nif_name, const QString& profile = QStringLiteral("static"));
  void cancel();
  // Delivers the pending result through completed / skyrimCompleted with report.run.decision = "recovered".
  void acceptRecovered();
  // Drops the pending result and its staging folder; emits failed().
  void discardRecovered();
  // Milliseconds for the version check, the whole run and the wait for Blender to exit after its OK marker. 0 keeps the
  // default (15 s; 5 min for a GLB preview and 10 min for a Skyrim job; 60 s).
  void set_timeouts(int version_ms, int run_ms, int exit_grace_ms);

 signals:
  // report.run holds the run facts.
  void completed(const QString& glb_path, const QVariantMap& report);
  // The stage holds Data/meshes, Data/textures and work.blend; it stays valid while the direct receiver runs.
  void skyrimCompleted(const QString& operation, const QString& stage, const QVariantMap& report);
  void progressed(const QString& step, double fraction);
  void failed(const QString& message);
  // A finished, verified result after a bad exit: run is lastRun(). Nothing is delivered until a decision.
  void recoverable(const QVariantMap& run);

 private:
  enum class Phase { idle, version, conversion, awaitingDecision };
  void start(const QString& executable, const QString& source);
  void start_conversion();
  void read_lines();
  void finish_failure(const QString& message);
  void fail_with(BlenderRunFacts facts);
  void on_finished(int exit_code, QProcess::ExitStatus status);
  void deliver(const QString& decision);
  void drop_pending();
  QProcess process_;
  QTimer timeout_;
  QTimer exit_grace_;
  QElapsedTimer elapsed_;
  Phase phase_ = Phase::idle;
  bool cancelled_ = false;
  bool timed_out_ = false;
  bool hung_ = false;
  bool marker_ok_ = false;
  bool marker_error_ = false;
  bool failed_to_start_ = false;
  int version_timeout_ms_ = 15000;
  int run_timeout_ms_ = 0;  // 0: 5 min for a GLB preview, 10 min for a Skyrim job
  int exit_grace_ms_ = 60000;
  QString executable_;
  QString source_;
  QString script_;
  QString output_;
  QString report_;
  QString job_;        // empty for a GLB conversion
  QString operation_;
  QString crash_log_;
  QString blender_temp_;  // Blender's session temp folder, from the scripts' SNS_FORGE_TEMPDIR line
  QByteArray stdout_;
  QByteArray line_;
  QByteArray stderr_;
  QVariantMap pending_report_;
  QVariantMap last_run_;
  std::unique_ptr<QTemporaryDir> stage_;
};

}  // namespace sns::app
