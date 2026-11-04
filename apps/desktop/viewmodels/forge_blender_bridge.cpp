#include "forge_blender_bridge.hpp"

#include <QDir>
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
    const auto chunk = process_.readAllStandardOutput();
    if (stdout_.size() < 65536) stdout_ += chunk.left(65536 - stdout_.size());
    if (!job_.isEmpty() && phase_ == Phase::conversion) { progress_line_ += chunk; read_progress(); }
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

namespace {
QString stage_script(const QTemporaryDir& stage, const QString& name) {
  const auto path = stage.filePath(name);
  QFile embedded(":/forge/" + name), script(path);
  if (!embedded.open(QIODevice::ReadOnly) || !script.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
      script.write(embedded.readAll()) <= 0 || !script.flush())
    throw std::runtime_error("Cannot stage the versioned Blender script");
  return path;
}

void check_inputs(const QFileInfo& program, const QFileInfo& input, const QStringList& extensions) {
  if (!program.isFile() || !program.isExecutable() || !input.isFile() || !input.isReadable() ||
      input.size() <= 0 || input.size() > 2LL * 1024 * 1024 * 1024 || !extensions.contains(input.suffix().toLower()))
    throw std::invalid_argument("Select Blender and a readable " + extensions.join(", ").toUpper().toStdString() +
                                " file up to 2 GiB");
}
}  // namespace

void ForgeBlenderBridge::convert(const QString& executable, const QString& source) {
  if (busy()) throw std::logic_error("A Blender conversion is already running");
  const QFileInfo program(executable), input(source);
  check_inputs(program, input, {"fbx", "obj", "blend"});
  stage_ = std::make_unique<QTemporaryDir>();
  if (!stage_->isValid()) { stage_.reset(); throw std::runtime_error("Cannot create Blender staging directory"); }
  try { script_ = stage_script(*stage_, "forge_bridge.py"); } catch (...) { stage_.reset(); throw; }
  output_ = stage_->filePath("model.glb");
  report_ = stage_->filePath("report.json");
  job_.clear(); operation_.clear();
  start(program.canonicalFilePath(), input.canonicalFilePath());
}

void ForgeBlenderBridge::runSkyrimJob(const QString& executable, const QString& operation, const QString& source,
                                      const QString& texture_prefix, const QString& nif_name, const QString& profile) {
  if (busy()) throw std::logic_error("A Blender job is already running");
  if (operation != "inspect" && operation != "exportNif") throw std::invalid_argument("Unknown Skyrim job operation");
  if (profile != "static" && profile != "skinned") throw std::invalid_argument("Unknown Skyrim conversion profile");
  static const QRegularExpression prefix(R"(^[A-Za-z0-9_\- ]{1,64}(\\[A-Za-z0-9_\- ]{1,64}){0,3}$)");
  static const QRegularExpression name(R"(^[A-Za-z0-9_\-]{1,64}\.nif$)");
  if (!prefix.match(texture_prefix).hasMatch() || !name.match(nif_name).hasMatch())
    throw std::invalid_argument("Invalid Skyrim asset folder or NIF name");
  const QFileInfo program(executable), input(source);
  check_inputs(program, input, {"blend", "fbx", "obj", "glb", "gltf"});
  stage_ = std::make_unique<QTemporaryDir>();
  if (!stage_->isValid()) { stage_.reset(); throw std::runtime_error("Cannot create Blender staging directory"); }
  try {
    script_ = stage_script(*stage_, "forge_skyrim.py");
    report_ = stage_->filePath("report.json");
    job_ = stage_->filePath("job.json");
    const QJsonObject job{{"schema", "sns.forge.skyrim.job/1"}, {"operation", operation},
                          {"source", QDir::toNativeSeparators(input.canonicalFilePath())},
                          {"stage", QDir::toNativeSeparators(stage_->path())},
                          {"report", QDir::toNativeSeparators(report_)}, {"texturePrefix", texture_prefix},
                          {"nifName", nif_name}, {"targetGame", "SKYRIMSE"}, {"applySafeTransforms", true},
                          {"profile", profile}};
    QFile file(job_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(QJsonDocument(job).toJson()) <= 0 || !file.flush())
      throw std::runtime_error("Cannot write the Blender job file");
  } catch (...) { stage_.reset(); job_.clear(); throw; }
  operation_ = operation;
  output_.clear();
  start(program.canonicalFilePath(), input.canonicalFilePath());
}

void ForgeBlenderBridge::start(const QString& executable, const QString& source) {
  executable_ = executable;
  source_ = source;
  stdout_.clear(); stderr_.clear(); progress_line_.clear(); cancelled_ = false; timed_out_ = false;
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
  job_.clear();
  emit failed(message);
}

void ForgeBlenderBridge::start_conversion() {
  stdout_.clear(); stderr_.clear();
  phase_ = Phase::conversion;
  process_.setProgram(executable_);
  if (job_.isEmpty())
    process_.setArguments({"--background", "--factory-startup", "--disable-autoexec", "--python-exit-code", "17",
                           "--python", script_, "--", source_, output_, report_});
  else  // the job file names the source; the script enables PyNifly from the user's add-ons itself
    process_.setArguments({"--background", "--factory-startup", "--disable-autoexec", "--python-exit-code", "17",
                           "--python", script_, "--", job_});
  timeout_.start((job_.isEmpty() ? 5 : 10) * 60 * 1000);
  process_.start();
}

void ForgeBlenderBridge::read_progress() {
  // "SNS_FORGE_PROGRESS {"step": "export", "fraction": 0.7}" lines; anything else is ignored.
  for (qsizetype end; (end = progress_line_.indexOf('\n')) >= 0;) {
    const auto line = progress_line_.left(end).trimmed();
    progress_line_.remove(0, end + 1);
    if (!line.startsWith("SNS_FORGE_PROGRESS ")) continue;
    const auto data = QJsonDocument::fromJson(line.mid(19)).object();
    if (!data.value("step").isString()) continue;
    emit progressed(data.value("step").toString(), qBound(0.0, data.value("fraction").toDouble(), 1.0));
  }
  if (progress_line_.size() > 4096) progress_line_.clear();
}

void ForgeBlenderBridge::on_finished(int exit_code, QProcess::ExitStatus status) {
  if (!busy()) return;
  timeout_.stop();
  if (cancelled_) { finish_failure(tr("Conversión de Blender cancelada.")); return; }
  if (timed_out_) { finish_failure(tr("Blender superó el tiempo máximo de ejecución.")); return; }
  if (phase_ == Phase::version) {
    if (status != QProcess::NormalExit || exit_code != 0) {
      finish_failure(tr("Blender terminó con un error (código %1).").arg(exit_code));
      return;
    }
    const QRegularExpression version(R"(^Blender ([0-9]+)\.([0-9]+))");
    const auto match = version.match(QString::fromUtf8(stdout_));
    if (!match.hasMatch() || match.captured(1).toInt() < 4 || match.captured(1).toInt() > 5) {
      finish_failure(tr("Se requiere Blender 4 o 5 verificable.")); return;
    }
    start_conversion();
    return;
  }
  if (!job_.isEmpty()) {
    QFile file(report_);
    QJsonParseError error{};
    const auto document = file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024
                              ? QJsonDocument::fromJson(file.readAll(), &error) : QJsonDocument();
    const auto data = document.object();
    if (!document.isNull() && error.error == QJsonParseError::NoError &&
        data.value("schema").toString() == "sns.forge.skyrim/1" &&
        data.value("status").toString() == "ok" && data.value("operation").toString() == operation_) {
      phase_ = Phase::idle;
      const auto operation = operation_;
      job_.clear();
      emit skyrimCompleted(operation, stage_->path(), data.toVariantMap());
      stage_.reset();
      return;
    }
    if (status != QProcess::NormalExit || exit_code != 0) {
      const QString detail = data.value("message").toString().left(600);
      finish_failure(detail.isEmpty() ? tr("Blender terminó con un error (código %1).").arg(exit_code)
                                      : tr("Blender terminó con un error (código %1): %2").arg(exit_code).arg(detail));
      return;
    }
    finish_failure(tr("El informe de Blender no es un resultado válido del trabajo Skyrim."));
    return;
  }
  QFile file(report_);
  if (file.open(QIODevice::ReadOnly) && file.size() <= 65536 && QFileInfo(output_).isFile()) {
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto data = document.object();
    if (error.error == QJsonParseError::NoError && document.isObject() &&
        data.value("schema").toInt() == 1 && data.value("status").toString() == "ok" &&
        data.value("meshes").toInt() > 0 && data.value("outputBytes").toInteger() == QFileInfo(output_).size()) {
      const auto result = data.toVariantMap();
      phase_ = Phase::idle;
      emit completed(output_, result);
      stage_.reset();
      return;
    }
  }
  if (status != QProcess::NormalExit || exit_code != 0) {
    finish_failure(tr("Blender terminó con un error (código %1).").arg(exit_code));
    return;
  }
  finish_failure(tr("El informe de Blender no coincide con el modelo generado."));
}

}  // namespace sns::app
