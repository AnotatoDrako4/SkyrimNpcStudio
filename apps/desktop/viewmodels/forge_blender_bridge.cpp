#include "forge_blender_bridge.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <stdexcept>

namespace sns::app {

ForgeBlenderBridge::ForgeBlenderBridge(QObject* parent) : QObject(parent) {
  timeout_.setSingleShot(true);
  connect(&timeout_, &QTimer::timeout, this, [this] { timed_out_ = true; process_.kill(); });
  connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
    if (stdout_.size() < 65536) stdout_ += process_.readAllStandardOutput().left(65536 - stdout_.size());
    else process_.readAllStandardOutput();
  });
  connect(&process_, &QProcess::readyReadStandardError, this, [this] {
    if (stderr_.size() < 65536) stderr_ += process_.readAllStandardError().left(65536 - stderr_.size());
    else process_.readAllStandardError();
  });
  connect(&process_, &QProcess::finished, this, &ForgeBlenderBridge::on_finished);
  connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) finish_failure(tr("No se pudo iniciar Blender."));
  });
}

ForgeBlenderBridge::~ForgeBlenderBridge() {
  timeout_.stop();
  if (process_.state() != QProcess::NotRunning) { process_.kill(); process_.waitForFinished(3000); }
}

void ForgeBlenderBridge::convert(const QString& executable, const QString& source) {
  if (busy()) throw std::logic_error("A Blender conversion is already running");
  const QFileInfo program(executable), input(source);
  const auto extension = input.suffix().toLower();
  if (!program.isFile() || !program.isExecutable() || !input.isFile() || !input.isReadable() ||
      input.size() <= 0 || input.size() > 2LL * 1024 * 1024 * 1024 ||
      (extension != "fbx" && extension != "obj" && extension != "blend"))
    throw std::invalid_argument("Select Blender and a readable FBX, OBJ or BLEND file up to 2 GiB");
  stage_ = std::make_unique<QTemporaryDir>();
  if (!stage_->isValid()) { stage_.reset(); throw std::runtime_error("Cannot create Blender staging directory"); }
  script_ = stage_->filePath("forge_bridge.py");
  output_ = stage_->filePath("model.glb");
  report_ = stage_->filePath("report.json");
  QFile embedded(":/forge/forge_bridge.py"), script(script_);
  if (!embedded.open(QIODevice::ReadOnly) || !script.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
      script.write(embedded.readAll()) <= 0 || !script.flush()) {
    stage_.reset(); throw std::runtime_error("Cannot stage the versioned Blender script");
  }
  script.close();
  executable_ = program.canonicalFilePath();
  source_ = input.canonicalFilePath();
  stdout_.clear(); stderr_.clear(); cancelled_ = false; timed_out_ = false;
  phase_ = Phase::version;
  process_.setProgram(executable_);
  process_.setArguments({"--version"});
  timeout_.start(15000);
  process_.start();
}

void ForgeBlenderBridge::cancel() {
  if (!busy()) return;
  cancelled_ = true;
  timeout_.stop();
  if (process_.state() != QProcess::NotRunning) process_.kill();
  else finish_failure(tr("Conversión de Blender cancelada."));
}

void ForgeBlenderBridge::finish_failure(const QString& message) {
  if (!busy()) return;
  timeout_.stop(); phase_ = Phase::idle;
  stage_.reset();
  emit failed(message);
}

void ForgeBlenderBridge::start_conversion() {
  stdout_.clear(); stderr_.clear();
  phase_ = Phase::conversion;
  process_.setProgram(executable_);
  process_.setArguments({"--background", "--factory-startup", "--disable-autoexec", "--python-exit-code", "17",
                         "--python", script_, "--", source_, output_, report_});
  timeout_.start(5 * 60 * 1000);
  process_.start();
}

void ForgeBlenderBridge::on_finished(int exit_code, QProcess::ExitStatus status) {
  if (!busy()) return;
  timeout_.stop();
  if (cancelled_) { finish_failure(tr("Conversión de Blender cancelada.")); return; }
  if (timed_out_) { finish_failure(tr("Blender superó el tiempo máximo de ejecución.")); return; }
  if (status != QProcess::NormalExit || exit_code != 0) {
    finish_failure(tr("Blender terminó con un error (código %1).").arg(exit_code)); return;
  }
  if (phase_ == Phase::version) {
    const QRegularExpression version(R"(^Blender ([0-9]+)\.([0-9]+))");
    const auto match = version.match(QString::fromUtf8(stdout_));
    if (!match.hasMatch() || match.captured(1).toInt() < 4 || match.captured(1).toInt() > 5) {
      finish_failure(tr("Se requiere Blender 4 o 5 verificable.")); return;
    }
    start_conversion();
    return;
  }
  QFile file(report_);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 65536 || !QFileInfo(output_).isFile()) {
    finish_failure(tr("Blender no produjo un informe y GLB válidos.")); return;
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &error);
  const auto data = document.object();
  if (error.error != QJsonParseError::NoError || !document.isObject() ||
      data.value("schema").toInt() != 1 || data.value("status").toString() != "ok" ||
      data.value("meshes").toInt() <= 0 || data.value("outputBytes").toInteger() != QFileInfo(output_).size()) {
    finish_failure(tr("El informe de Blender no coincide con el modelo generado.")); return;
  }
  const auto result = data.toVariantMap();
  phase_ = Phase::idle;
  emit completed(output_, result);  // The stage remains valid while the direct receiver imports it.
  stage_.reset();
}

}  // namespace sns::app
