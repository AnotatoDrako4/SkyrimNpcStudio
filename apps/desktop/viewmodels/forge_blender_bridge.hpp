#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <memory>

namespace sns::app {

// Runs only the resource-embedded Blender script. The caller validates/imports its GLB before use.
class ForgeBlenderBridge final : public QObject {
  Q_OBJECT
 public:
  explicit ForgeBlenderBridge(QObject* parent = nullptr);
  ~ForgeBlenderBridge() override;
  bool busy() const { return phase_ != Phase::idle; }
  void convert(const QString& executable, const QString& source);
  void cancel();

 signals:
  void completed(const QString& glb_path, const QVariantMap& report);
  void failed(const QString& message);

 private:
  enum class Phase { idle, version, conversion };
  void start_conversion();
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
  QByteArray stdout_;
  QByteArray stderr_;
  std::unique_ptr<QTemporaryDir> stage_;
};

}  // namespace sns::app
