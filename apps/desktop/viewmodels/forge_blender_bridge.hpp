#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <memory>

namespace sns::app {

// Runs only the resource-embedded Blender scripts. The caller validates/imports their outputs before use.
class ForgeBlenderBridge final : public QObject {
  Q_OBJECT
 public:
  explicit ForgeBlenderBridge(QObject* parent = nullptr);
  ~ForgeBlenderBridge() override;
  bool busy() const { return phase_ != Phase::idle; }
  void convert(const QString& executable, const QString& source);
  // Runs tools/blender/forge_skyrim.py (contract sns.forge.skyrim/1): operation "inspect" or "exportNif" on a
  // copy of `source` inside a fresh staging folder. Blender receives only the path of a job file the bridge
  // writes; never Python text or a shell command. texture_prefix is "Word\word" (Data-relative folder under
  // meshes\ and textures\), nif_name "<slug>.nif". profile: "static" (Type A) or "skinned" (SkinnedMesh).
  void runSkyrimJob(const QString& executable, const QString& operation, const QString& source,
                    const QString& texture_prefix, const QString& nif_name, const QString& profile = QStringLiteral("static"));
  void cancel();

 signals:
  void completed(const QString& glb_path, const QVariantMap& report);
  // The stage holds Data/meshes, Data/textures and work.blend; it stays valid while the direct receiver runs.
  void skyrimCompleted(const QString& operation, const QString& stage, const QVariantMap& report);
  void progressed(const QString& step, double fraction);
  void failed(const QString& message);

 private:
  enum class Phase { idle, version, conversion };
  void start(const QString& executable, const QString& source);
  void start_conversion();
  void read_progress();
  void finish_failure(const QString& message);
  void on_finished(int exit_code, QProcess::ExitStatus status);
  QProcess process_;
  QTimer timeout_;
  Phase phase_ = Phase::idle;
  bool cancelled_ = false;
  bool timed_out_ = false;
  QString executable_;
  QString source_;
  QString script_;
  QString output_;
  QString report_;
  QString job_;        // empty for a GLB conversion
  QString operation_;
  QByteArray stdout_;
  QByteArray progress_line_;
  QByteArray stderr_;
  std::unique_ptr<QTemporaryDir> stage_;
};

}  // namespace sns::app
