#include "forge_blender_bridge.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <stdexcept>

#include "stale_temp_folders.hpp"

namespace sns::app {

namespace {
QString t(const char* text) { return QCoreApplication::translate("ForgeBlenderBridge", text); }

QString hex_code(qint64 code) {
  return QStringLiteral("0x") + QString::number(static_cast<quint32>(code), 16).toUpper().rightJustified(8, '0');
}

QString exit_meaning(qint64 code) {
  switch (static_cast<quint32>(code)) {
    case 0xC0000005u: return t("violación de acceso (EXCEPTION_ACCESS_VIOLATION)");
    case 0xC00000FDu: return t("desbordamiento de pila");
    case 0xC0000409u: return t("terminación forzada por el propio proceso (fail-fast)");
    case 0xC0000374u: return t("corrupción de memoria (heap)");
    case 0xC0000017u: return t("memoria insuficiente");
    case 0xE06D7363u: return t("excepción C++ no controlada");
    default: return {};
  }
}

QString file_sha256(const QString& path) {
  QFile file(path);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
  return QString::fromLatin1(hash.result().toHex());
}

struct Check {
  QString state;
  QString detail;
};

// Size, then format magic, then SHA-256 against what the script reported after writing the file.
Check verify_file(const QString& path, qint64 expected_bytes, const QString& expected_sha, const QByteArray& magic) {
  const QFileInfo info(path);
  if (!info.isFile()) return {QStringLiteral("missing"), t("el archivo generado no existe")};
  if (expected_bytes <= 0 || expected_sha.size() != 64)
    return {QStringLiteral("invalid"), t("el informe de Blender no declara el tamaño y el SHA-256 del archivo")};
  if (info.size() < expected_bytes)
    return {QStringLiteral("truncated"), t("%1 de %2 bytes escritos").arg(info.size()).arg(expected_bytes)};
  if (info.size() != expected_bytes)
    return {QStringLiteral("invalid"), t("tamaño %1 bytes; el informe dice %2").arg(info.size()).arg(expected_bytes)};
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {QStringLiteral("missing"), t("no se puede leer el archivo generado")};
  const auto head = file.read(qMax<qint64>(magic.size(), 12));
  file.close();
  if (!head.startsWith(magic)) return {QStringLiteral("invalid"), t("la cabecera no corresponde al formato esperado")};
  if (magic == "glTF") {  // version 2 and a declared length equal to the file
    const auto le32 = [&](int at) {
      return quint32(quint8(head[at])) | quint32(quint8(head[at + 1])) << 8 | quint32(quint8(head[at + 2])) << 16 |
             quint32(quint8(head[at + 3])) << 24;
    };
    if (head.size() < 12 || le32(4) != 2 || le32(8) != static_cast<quint32>(info.size()))
      return {QStringLiteral("truncated"), t("la longitud declarada por el GLB no coincide con el archivo")};
  }
  const auto sha = file_sha256(path);
  if (sha.compare(expected_sha, Qt::CaseInsensitive) != 0)
    return {QStringLiteral("hashMismatch"), t("SHA-256 %1; el informe dice %2").arg(sha.left(12), expected_sha.left(12))};
  return {QStringLiteral("verified"), sha};
}

// Blender's own per-session temp folder (bpy.app.tempdir, "<temp>/blender_xxxx"). Blender removes it when it exits
// normally; a crash, a kill or the scripts' fast exit leave it behind. Only that exact folder shape is removed.
void remove_blender_session_dir(const QString& reported) {
  if (reported.isEmpty()) return;
  const QFileInfo dir(QDir::cleanPath(QDir::fromNativeSeparators(reported)));
  static const QRegularExpression name(QStringLiteral(R"(^blender_[A-Za-z0-9]{1,32}$)"));
  if (!dir.isDir() || !name.match(dir.fileName()).hasMatch()) return;
  if (QFileInfo(dir.absolutePath()).canonicalFilePath() != QFileInfo(QDir::tempPath()).canonicalFilePath()) return;
  QDir(dir.absoluteFilePath()).removeRecursively();
}

QString stage_script(const QTemporaryDir& stage, const QString& name) {
  const auto path = stage.filePath(name);
  QFile embedded(":/forge/" + name), script(path);
  if (!embedded.open(QIODevice::ReadOnly) || !script.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
      script.write(embedded.readAll()) <= 0 || !script.flush())
    throw std::runtime_error("Cannot stage the versioned Blender script");
  return path;
}

std::unique_ptr<QTemporaryDir> make_stage() {
  auto stage = std::make_unique<QTemporaryDir>(
      QDir::temp().filePath(QStringLiteral("sns-forge-blender-%1-XXXXXX").arg(QCoreApplication::applicationPid())));
  if (!stage->isValid()) throw std::runtime_error("Cannot create Blender staging directory");
  return stage;
}

void check_inputs(const QFileInfo& program, const QFileInfo& input, const QStringList& extensions) {
  if (!program.isFile() || !program.isExecutable() || !input.isFile() || !input.isReadable() ||
      input.size() <= 0 || input.size() > 2LL * 1024 * 1024 * 1024 || !extensions.contains(input.suffix().toLower()))
    throw std::invalid_argument("Select Blender and a readable " + extensions.join(", ").toUpper().toStdString() +
                                " file up to 2 GiB");
}
}  // namespace

BlenderVerdict judge_blender_run(const BlenderRunFacts& f) {
  using Exit = BlenderRunFacts::Exit;
  if (f.exit == Exit::cancelled || f.exit == Exit::failedToStart) return BlenderVerdict::failed;
  const bool finished = f.marker_ok && !f.marker_error && f.report == QStringLiteral("ok") &&
                        (f.artifact == QStringLiteral("verified") || f.artifact == QStringLiteral("notApplicable"));
  if (!finished) return BlenderVerdict::failed;
  if (f.exit == Exit::normal && f.exit_code == 0) return BlenderVerdict::accepted;
  return BlenderVerdict::recoverable;  // nonZero, crashed, hungAfterResult, or timedOut after the marker
}

QString exit_name(BlenderRunFacts::Exit exit) {
  switch (exit) {
    case BlenderRunFacts::Exit::normal: return QStringLiteral("normal");
    case BlenderRunFacts::Exit::nonZero: return QStringLiteral("nonZero");
    case BlenderRunFacts::Exit::crashed: return QStringLiteral("crashed");
    case BlenderRunFacts::Exit::timedOut: return QStringLiteral("timedOut");
    case BlenderRunFacts::Exit::hungAfterResult: return QStringLiteral("hungAfterResult");
    case BlenderRunFacts::Exit::cancelled: return QStringLiteral("cancelled");
    case BlenderRunFacts::Exit::failedToStart: return QStringLiteral("failedToStart");
  }
  return {};
}

QString describe_blender_run(const BlenderRunFacts& f, BlenderVerdict verdict) {
  using Exit = BlenderRunFacts::Exit;
  const auto seconds = QString::number(f.elapsed_ms / 1000.0, 'f', 1);
  const auto meaning = exit_meaning(f.exit_code);
  const auto code = meaning.isEmpty() ? hex_code(f.exit_code) : hex_code(f.exit_code) + ", " + meaning;
  QString how;  // how the process ended
  switch (f.exit) {
    case Exit::crashed: how = t("Blender se cerró con un fallo (%1)").arg(code); break;
    case Exit::nonZero: how = t("Blender terminó con el código de error %1").arg(f.exit_code); break;
    case Exit::hungAfterResult: how = t("Blender no se cerró después de terminar y se detuvo a los %1 s").arg(seconds); break;
    case Exit::timedOut: how = t("Blender superó el tiempo máximo (%1 s) y se detuvo").arg(seconds); break;
    default: break;
  }
  if (verdict == BlenderVerdict::accepted) return t("Blender terminó correctamente en %1 s.").arg(seconds);
  if (verdict == BlenderVerdict::recoverable)
    return t("%1 después de escribir el resultado. El archivo pasó la verificación (tamaño, cabecera y SHA-256), pero no se ha "
             "añadido nada: recupéralo de forma explícita o descártalo y vuelve a intentarlo.").arg(how);
  switch (f.exit) {
    case Exit::cancelled: return t("Conversión de Blender cancelada. No se añadió nada al proyecto.");
    case Exit::failedToStart: return t("No se pudo iniciar Blender. Comprueba su ruta en «Configurar Blender».");
    default: break;
  }
  QString why;
  if (f.report == QStringLiteral("error"))
    why = t("Blender no pudo completar el trabajo: %1").arg(f.report_message.isEmpty() ? t("sin detalle") : f.report_message);
  else if (f.report != QStringLiteral("ok"))
    why = f.report == QStringLiteral("invalid") ? t("El informe de Blender no es un resultado válido de este trabajo.")
                                               : t("Blender no llegó a escribir su informe: no hay resultado que recuperar.");
  else if (f.artifact != QStringLiteral("verified") && f.artifact != QStringLiteral("notApplicable"))
    why = t("El archivo generado no supera la verificación (%1): no se usa.").arg(f.artifact_detail);
  else if (!f.marker_ok)
    why = t("Blender terminó sin confirmar el final del trabajo: el resultado no se usa.");
  const auto prefix = how.isEmpty() ? QString() : how + ". ";
  auto text = prefix + why;
  if (f.exit == Exit::timedOut) text += t(" Prueba con un modelo más ligero o revisa si Blender se ha quedado esperando.");
  else if (f.exit == Exit::crashed || f.exit == Exit::nonZero) text += t(" Vuelve a intentarlo; si se repite, abre el modelo en Blender y guárdalo de nuevo.");
  if (!f.crash_log.isEmpty()) text += t(" Informe de fallo de Blender: %1").arg(QDir::toNativeSeparators(f.crash_log));
  return text.trimmed();
}

QVariantMap to_variant(const BlenderRunFacts& f, BlenderVerdict verdict) {
  static const char* verdicts[] = {"accepted", "recoverable", "failed"};
  return {{"verdict", QString::fromLatin1(verdicts[static_cast<int>(verdict)])},
          {"exit", exit_name(f.exit)},
          {"exitCode", f.exit_code},
          {"exitHex", hex_code(f.exit_code)},
          {"exitMeaning", exit_meaning(f.exit_code)},
          {"seconds", f.elapsed_ms / 1000.0},
          {"markerOk", f.marker_ok},
          {"markerError", f.marker_error},
          {"report", f.report},
          {"reportMessage", f.report_message},
          {"artifact", f.artifact},
          {"artifactDetail", f.artifact_detail},
          {"crashLog", QDir::toNativeSeparators(f.crash_log)},
          {"stderrTail", f.stderr_tail},
          {"message", describe_blender_run(f, verdict)}};
}

ForgeBlenderBridge::ForgeBlenderBridge(QObject* parent) : QObject(parent) {
  remove_stale_temp_folders(QStringLiteral("sns-forge-blender-"));  // stages of studios that ended without cleaning up
  timeout_.setSingleShot(true);
  exit_grace_.setSingleShot(true);
  connect(&timeout_, &QTimer::timeout, this, [this] { timed_out_ = true; process_.kill(); });
  connect(&exit_grace_, &QTimer::timeout, this, [this] { hung_ = true; process_.kill(); });
  connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
    const auto chunk = process_.readAllStandardOutput();
    if (stdout_.size() < 65536) stdout_ += chunk.left(65536 - stdout_.size());
    if (phase_ == Phase::conversion) { line_ += chunk; read_lines(); }
  });
  connect(&process_, &QProcess::readyReadStandardError, this, [this] {
    stderr_ += process_.readAllStandardError();
    if (stderr_.size() > 16384) stderr_.remove(0, stderr_.size() - 16384);  // the tail holds the crash lines
  });
  connect(&process_, &QProcess::finished, this, &ForgeBlenderBridge::on_finished);
  connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || !busy()) return;
    failed_to_start_ = true;
    // Reported from the event loop: QProcess::start() emits this synchronously, inside convert()/runSkyrimJob().
    QMetaObject::invokeMethod(this, [this] {
      if (!busy() || !failed_to_start_) return;
      BlenderRunFacts facts;
      facts.exit = BlenderRunFacts::Exit::failedToStart;
      facts.elapsed_ms = elapsed_.isValid() ? elapsed_.elapsed() : 0;
      fail_with(facts);
    }, Qt::QueuedConnection);
  });
}

ForgeBlenderBridge::~ForgeBlenderBridge() {
  timeout_.stop();
  exit_grace_.stop();
  if (process_.state() != QProcess::NotRunning) { process_.kill(); process_.waitForFinished(3000); }
}

void ForgeBlenderBridge::set_timeouts(int version_ms, int run_ms, int exit_grace_ms) {
  version_timeout_ms_ = version_ms > 0 ? version_ms : 15000;
  run_timeout_ms_ = run_ms > 0 ? run_ms : 0;
  exit_grace_ms_ = exit_grace_ms > 0 ? exit_grace_ms : 60000;
}

void ForgeBlenderBridge::drop_pending() {
  if (phase_ != Phase::awaitingDecision) return;
  phase_ = Phase::idle;
  pending_report_.clear();
  stage_.reset();
}

void ForgeBlenderBridge::convert(const QString& executable, const QString& source) {
  if (busy()) throw std::logic_error("A Blender conversion is already running");
  const QFileInfo program(executable), input(source);
  check_inputs(program, input, {"fbx", "obj", "blend"});
  drop_pending();  // a new run replaces an undecided result
  stage_ = make_stage();
  try {
    script_ = stage_script(*stage_, "forge_bridge.py");
    stage_script(*stage_, "forge_resources.py");  // imported by the script: missing textures relinked by name
  } catch (...) { stage_.reset(); throw; }
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
  drop_pending();
  stage_ = make_stage();
  try {
    script_ = stage_script(*stage_, "forge_skyrim.py");
    stage_script(*stage_, "forge_resources.py");
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
  stdout_.clear(); stderr_.clear(); line_.clear(); crash_log_.clear(); blender_temp_.clear();
  cancelled_ = timed_out_ = hung_ = marker_ok_ = marker_error_ = failed_to_start_ = false;
  last_run_.clear();
  elapsed_.start();
  phase_ = Phase::version;
  process_.setProgram(executable_);
  process_.setArguments({"--version"});
  timeout_.start(version_timeout_ms_);
  process_.start();
}

void ForgeBlenderBridge::cancel() {
  if (!busy()) return;
  cancelled_ = true;
  timeout_.stop();
  exit_grace_.stop();
  if (process_.state() != QProcess::NotRunning) process_.kill();
  else {
    BlenderRunFacts facts;
    facts.exit = BlenderRunFacts::Exit::cancelled;
    fail_with(facts);
  }
}

void ForgeBlenderBridge::finish_failure(const QString& message) {
  if (!busy()) return;
  timeout_.stop(); exit_grace_.stop(); phase_ = Phase::idle;
  stage_.reset();
  job_.clear();
  emit failed(message);
}

void ForgeBlenderBridge::fail_with(BlenderRunFacts facts) {
  facts.crash_log = crash_log_;
  facts.stderr_tail = QString::fromUtf8(stderr_.right(2048));
  last_run_ = to_variant(facts, BlenderVerdict::failed);
  finish_failure(last_run_.value("message").toString());
}

void ForgeBlenderBridge::start_conversion() {
  stdout_.clear(); stderr_.clear(); line_.clear();
  phase_ = Phase::conversion;
  process_.setProgram(executable_);
  if (job_.isEmpty())
    process_.setArguments({"--background", "--factory-startup", "--disable-autoexec", "--python-exit-code", "17",
                           "--python", script_, "--", source_, output_, report_});
  else  // the job file names the source; the script enables PyNifly from the user's add-ons itself
    process_.setArguments({"--background", "--factory-startup", "--disable-autoexec", "--python-exit-code", "17",
                           "--python", script_, "--", job_});
  timeout_.start(run_timeout_ms_ > 0 ? run_timeout_ms_ : (job_.isEmpty() ? 5 : 10) * 60 * 1000);
  process_.start();
}

void ForgeBlenderBridge::read_lines() {
  // "SNS_FORGE_PROGRESS {"step": "export", "fraction": 0.7}", the scripts' OK/ERROR markers and Blender's
  // "Writing: <file>.crash.txt"; anything else is ignored.
  static const QRegularExpression crash(QStringLiteral(R"(^Writing: (.+\.crash\.txt)$)"));
  for (qsizetype end; (end = line_.indexOf('\n')) >= 0;) {
    const auto line = line_.left(end).trimmed();
    line_.remove(0, end + 1);
    if (line.startsWith("SNS_FORGE_PROGRESS ")) {
      const auto data = QJsonDocument::fromJson(line.mid(19)).object();
      if (data.value("step").isString())
        emit progressed(data.value("step").toString(), qBound(0.0, data.value("fraction").toDouble(), 1.0));
    } else if (line == "SNS_FORGE_BRIDGE_OK" || line == "SNS_FORGE_SKYRIM_OK") {
      marker_ok_ = true;
      exit_grace_.start(exit_grace_ms_);  // the work is done: Blender only has to exit now
    } else if (line.startsWith("SNS_FORGE_BRIDGE_ERROR") || line.startsWith("SNS_FORGE_SKYRIM_ERROR")) {
      marker_error_ = true;
    } else if (line.startsWith("SNS_FORGE_TEMPDIR ")) {  // first line of the scripts: known even if the run is killed
      blender_temp_ = QString::fromUtf8(line.mid(18)).trimmed();
    } else if (const auto match = crash.match(QString::fromUtf8(line)); match.hasMatch()) {
      crash_log_ = match.captured(1).trimmed();
    }
  }
  if (line_.size() > 8192) line_.clear();
}

void ForgeBlenderBridge::on_finished(int exit_code, QProcess::ExitStatus status) {
  if (!busy() || failed_to_start_) return;
  timeout_.stop();
  exit_grace_.stop();
  using Exit = BlenderRunFacts::Exit;
  BlenderRunFacts facts;
  facts.exit_code = static_cast<qint64>(static_cast<quint32>(exit_code));  // NTSTATUS codes stay readable (0xC0000005)
  facts.elapsed_ms = elapsed_.elapsed();
  facts.exit = cancelled_ ? Exit::cancelled
             : hung_ ? Exit::hungAfterResult
             : timed_out_ ? Exit::timedOut
             : status == QProcess::CrashExit ? Exit::crashed
             : exit_code != 0 ? Exit::nonZero : Exit::normal;
  if (phase_ == Phase::version) {
    if (facts.exit != Exit::normal) { fail_with(facts); return; }
    const QRegularExpression version(R"(^Blender ([0-9]+)\.([0-9]+))");
    const auto match = version.match(QString::fromUtf8(stdout_));
    if (!match.hasMatch() || match.captured(1).toInt() < 4 || match.captured(1).toInt() > 5) {
      last_run_ = to_variant(facts, BlenderVerdict::failed);
      last_run_["message"] = tr("Se requiere Blender 4 o 5 verificable.");
      finish_failure(last_run_.value("message").toString());
      return;
    }
    start_conversion();
    return;
  }
  if (!line_.isEmpty()) { line_ += '\n'; read_lines(); }  // a last line without newline
  remove_blender_session_dir(blender_temp_);  // left behind by a crash, a kill (cancel, timeout) or the fast exit
  facts.marker_ok = marker_ok_;
  facts.marker_error = marker_error_;
  facts.crash_log = crash_log_;
  facts.stderr_tail = QString::fromUtf8(stderr_.right(2048));
  if (facts.exit == Exit::cancelled) { fail_with(facts); return; }

  // 1. Generation: the script's own report.
  QJsonObject data;
  {
    QFile file(report_);
    QJsonParseError error{};
    const auto document = file.open(QIODevice::ReadOnly) && file.size() <= 8 * 1024 * 1024
                              ? QJsonDocument::fromJson(file.readAll(), &error) : QJsonDocument();
    data = document.object();
    const bool exists = QFileInfo::exists(report_);
    const bool shaped = !document.isNull() && error.error == QJsonParseError::NoError && document.isObject() &&
                        (job_.isEmpty() ? data.value("schema").toInt() == 1
                                        : data.value("schema").toString() == "sns.forge.skyrim/1" &&
                                              data.value("operation").toString() == operation_);
    const auto status_text = data.value("status").toString();
    facts.report = !exists ? QStringLiteral("missing")
                 : !shaped ? QStringLiteral("invalid")
                 : status_text == "ok" ? QStringLiteral("ok")
                 : status_text == "error" ? QStringLiteral("error") : QStringLiteral("invalid");
    if (facts.report == QStringLiteral("ok") && job_.isEmpty() && data.value("meshes").toInt() <= 0)
      facts.report = QStringLiteral("invalid");
    facts.report_message = data.value("message").toString().left(600);
  }
  remove_blender_session_dir(data.value("blenderTempDir").toString());

  // 2. Artifact integrity: the file on disk is the one the script finished writing.
  if (facts.report == QStringLiteral("ok")) {
    if (job_.isEmpty()) {
      const auto check = verify_file(output_, data.value("outputBytes").toInteger(), data.value("outputSha256").toString(), "glTF");
      facts.artifact = check.state; facts.artifact_detail = check.detail;
    } else if (operation_ == QStringLiteral("exportNif")) {
      const auto exported = data.value("export").toObject();
      const auto relative = QDir::cleanPath(QDir::fromNativeSeparators(exported.value("output").toString()));
      if (relative.isEmpty() || relative.startsWith("..") || QDir::isAbsolutePath(relative)) {
        facts.artifact = QStringLiteral("invalid");
        facts.artifact_detail = tr("el informe no indica un NIF dentro de la carpeta de trabajo");
      } else {
        const auto check = verify_file(stage_->filePath(relative), exported.value("bytes").toInteger(),
                                       exported.value("sha256").toString(), "Gamebryo File Format");
        facts.artifact = check.state; facts.artifact_detail = check.detail;
      }
    } else {
      facts.artifact = QStringLiteral("notApplicable");  // inspect writes only its report
    }
  }

  // 3 + 4. Process state and verdict; incorporation is the receiver's job (or the user's decision).
  const auto verdict = judge_blender_run(facts);
  last_run_ = to_variant(facts, verdict);
  if (verdict == BlenderVerdict::failed) { finish_failure(last_run_.value("message").toString()); return; }
  pending_report_ = data.toVariantMap();
  if (verdict == BlenderVerdict::recoverable) {
    phase_ = Phase::awaitingDecision;
    emit recoverable(last_run_);
    return;
  }
  deliver(QStringLiteral("accepted"));
}

void ForgeBlenderBridge::deliver(const QString& decision) {
  last_run_["decision"] = decision;
  auto report = pending_report_;
  report["run"] = last_run_;
  pending_report_.clear();
  phase_ = Phase::idle;
  const auto operation = operation_;
  const auto output = output_;
  const bool job = !job_.isEmpty();
  job_.clear();
  // Alive while the receivers run; a run they start meanwhile gets a stage of its own and is not touched here.
  const auto stage = std::move(stage_);
  if (job) emit skyrimCompleted(operation, stage->path(), report);
  else emit completed(output, report);
}

void ForgeBlenderBridge::acceptRecovered() {
  if (phase_ != Phase::awaitingDecision) return;
  deliver(QStringLiteral("recovered"));
}

void ForgeBlenderBridge::discardRecovered() {
  if (phase_ != Phase::awaitingDecision) return;
  last_run_["decision"] = QStringLiteral("discarded");
  drop_pending();
  job_.clear();
  emit failed(tr("Resultado de Blender descartado: no se añadió nada al proyecto."));
}

}  // namespace sns::app
