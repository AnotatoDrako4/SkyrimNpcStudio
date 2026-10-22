#include "appearance_sources.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "diagnostics/crash_diagnostics.hpp"
#include "json_bridge.hpp"
#include "sns/core/text.hpp"
#include "sns/mo2/confined_file.hpp"
#include "sns/face/facegen.hpp"

namespace sns::app {
namespace fs = std::filesystem;

namespace {

constexpr qint64 kMaxGeometryBytes = 16ll * 1024 * 1024;
constexpr qint64 kMaxTintBytes = 32ll * 1024 * 1024;
constexpr int kMaxGuiSamples = 512;

QString now_iso() {
  const auto now = QDateTime::currentDateTime();
  return now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate);
}

QString sha256_hex(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }

QString key_of(const QString& data_path) { return qs(text::data_path_key(ss(data_path))); }

// The file `data_path` of ONE provider folder (mo2::confined_file: no absolute or `..` paths, no symbolic links, real
// location inside the folder). `why` says why a file that exists was refused; a mod without the file is no incident.
std::optional<fs::path> provider_file(const fs::path& root, const QString& data_path, QString* why) {
  const auto found = mo2::confined_file(root, ss(data_path));
  switch (found.result) {
    case mo2::ConfineResult::Found: return found.path;
    case mo2::ConfineResult::UnsafePath: *why = QStringLiteral("ruta de Data no válida"); break;
    case mo2::ConfineResult::SymbolicLink: *why = QStringLiteral("es un enlace simbólico"); break;
    case mo2::ConfineResult::EscapesFolder: *why = QStringLiteral("queda fuera de la carpeta del mod (enlace o junction)"); break;
    case mo2::ConfineResult::Absent:
    case mo2::ConfineResult::NotRegularFile: break;
  }
  return std::nullopt;
}

bool read_file(const fs::path& file, qint64 cap, QByteArray* out, QString* why) {
  std::error_code ec;
  const auto size = static_cast<qint64>(fs::file_size(file, ec));
  if (ec) {
    *why = QStringLiteral("no se puede leer su tamaño");
    return false;
  }
  if (size > cap) {
    *why = QStringLiteral("pesa %1 MB (límite %2 MB)").arg(size / (1024 * 1024)).arg(cap / (1024 * 1024));
    return false;
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    *why = QStringLiteral("no se puede abrir");
    return false;
  }
  out->resize(static_cast<qsizetype>(size));
  in.read(out->data(), size);
  if (in.gcount() != size) {
    *why = QStringLiteral("lectura incompleta");
    return false;
  }
  return true;
}

QString file_text(const fs::path& p) { return QString::fromStdWString(p.wstring()); }

QStringList list_of(const std::vector<std::string>& items) {
  QStringList out;
  for (const auto& i : items) out << qs(i);
  return out;
}

QVariantMap candidate_map(const appearance::SourceCandidate& c) {
  QVariantList overlay;
  for (const auto& o : c.overlay) overlay << QVariantMap{{"dataPath", qs(o.data_path)}, {"file", qs(o.file)}};
  return {{"id", qs(c.id)},
          {"label", qs(c.label)},
          {"effective", c.effective},
          {"status", qs(std::string(appearance::to_string(c.status)))},
          {"recordPlugin", qs(c.record_plugin)},
          {"recordMod", qs(c.record_mod)},
          {"facegenMod", qs(c.facegen_mod)},
          {"facetintMod", qs(c.facetint_mod)},
          {"geometryPath", qs(c.geometry_path)},
          {"tintPath", qs(c.tint_path)},
          {"codes", list_of(c.codes)},
          {"messages", list_of(c.messages)},
          {"renderable", c.renderable},
          {"renderNote", qs(c.render_note)},
          {"resolutionAvailable", c.resolution_available},
          {"requiresReview", c.requires_review},
          {"geometrySha256", qs(c.geometry_sha256)},
          {"tintSha256", qs(c.tint_sha256)},
          {"geometryBytes", static_cast<qlonglong>(c.geometry_bytes)},
          {"tintBytes", static_cast<qlonglong>(c.tint_bytes)},
          {"looseMods", list_of(c.loose_mods)},
          {"overlay", overlay}};
}

QVariantMap image_map(const QString& state, const QString& url, const QString& reason, const QString& created, qint64 ms, const QString& origin,
                      const QStringList& missing) {
  QVariantMap m{{"state", state}, {"url", url}, {"reason", reason}, {"createdAt", created}, {"renderMs", ms}, {"origin", origin}};
  if (!missing.isEmpty()) m["missing"] = missing;
  return m;
}

}  // namespace

struct AppearanceSources::Gathered {
  appearance::SourceMatrix matrix;
  QString digest;
  qint64 ms = 0;
  qint64 bytes = 0;
  QStringList skipped;
};

AppearanceSources::AppearanceSources(PortraitRenderer* portraits, QObject* parent) : QObject(parent), portraits_(portraits) {
  matrix_map_ = {{"state", "none"}};
  draft_map_ = {{"state", "empty"}};
  // The profile digest the portraits are bound to is computed in the background: once it exists, the matrix is built.
  connect(portraits_, &PortraitRenderer::changed, this, [this] {
    if (form_key_.isEmpty() || gathering_ || !snapshot_) return;
    const auto digest = portraits_->snapshot_digest();
    if (!digest.isEmpty() && qs(matrix_.profile_digest) != digest) start_gather();
  });
}

AppearanceSources::~AppearanceSources() {
  if (cancel_) *cancel_ = true;
}

QString AppearanceSources::variants_directory() const {
  const auto base = portraits_->cache_directory();
  return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("variants"));
}

// ------------------------------------------------------------------ profile and NPC

void AppearanceSources::set_profile(SnapshotPtr snapshot, const QString& profile_key) {
  ++serial_;
  if (cancel_) *cancel_ = true;
  snapshot_ = std::move(snapshot);
  profile_key_ = profile_key;
  form_key_.clear();
  pre_key_.clear();
  gathering_ = false;
  matrix_ = {};
  input_digest_.clear();
  images_.clear();
  a_.clear();
  b_.clear();
  matrix_map_ = {{"state", "none"}};
  book_ = {};
  book_path_.clear();
  book_unreadable_ = false;
  if (snapshot_) load_book();
  refresh_draft();
  ++image_revision_;
  emit matrixChanged();
  emit selectionChanged();
  emit imagesChanged();
}

void AppearanceSources::set_npc(const QString& form_key, const QString& name, const appearance::ControlInputs& control, const QVariantMap& details) {
  if (!snapshot_ || form_key.isEmpty()) return;
  // Everything the matrix depends on except the files themselves: unchanged => nothing to do (this is called on every
  // FaceGen rebuild of the face editor).
  appearance::SourceInputs probe;
  probe.npc_form_key = ss(form_key);
  probe.profile_digest = ss(portraits_->snapshot_digest());
  probe.control = control;
  const auto pre = qs(appearance::source_identity(probe));
  if (pre == pre_key_ && (gathering_ || matrix_map_.value("state").toString() == QLatin1String("ready"))) return;
  const bool other_npc = form_key != form_key_;
  ++serial_;
  if (cancel_) *cancel_ = true;
  form_key_ = form_key;
  name_ = name;
  control_ = control;
  details_ = details;
  pre_key_ = pre;
  if (other_npc) {
    images_.clear();
    a_.clear();
    b_.clear();
    ++image_revision_;
    emit imagesChanged();
    emit selectionChanged();
  }
  start_gather();
}

void AppearanceSources::start_gather() {
  const auto digest = portraits_->snapshot_digest();
  QVariantMap head{{"formKey", form_key_}, {"name", name_}};
  if (!snapshot_ || form_key_.isEmpty()) {
    matrix_map_ = {{"state", "none"}};
    emit matrixChanged();
    return;
  }
  if (digest.isEmpty()) {  // the profile digest is still being computed: the portraits' changed() calls back
    head["state"] = "gathering";
    matrix_map_ = head;
    gathering_ = false;
    emit matrixChanged();
    return;
  }
  ++gathers_;
  gathering_ = true;
  cancel_ = std::make_shared<std::atomic<bool>>(false);
  head["state"] = "gathering";
  matrix_map_ = head;
  emit matrixChanged();
  const int serial = serial_;
  QThreadPool::globalInstance()->start([self = QPointer<AppearanceSources>(this), snapshot = snapshot_, form_key = form_key_, digest, control = control_,
                                        details = details_, cancel = cancel_, serial] {
    QElapsedTimer clock;
    clock.start();
    auto result = std::make_shared<Gathered>();
    result->digest = digest;
    appearance::SourceInputs in;
    in.npc_form_key = ss(form_key);
    in.profile_digest = ss(digest);
    in.control = control;
    const auto geometry = qs(control.facegen.path);
    const auto tint = qs(control.facetint.path);
    // Record head parts that must have a shape of their name in a FaceGen (placeholders without a model have none).
    std::vector<std::string> editor_ids;
    for (const auto& part : details.value("headParts").toList()) {
      const auto m = part.toMap();
      if (m.value("modelPath").toString().isEmpty()) continue;
      editor_ids.push_back(ss(m.value("record").toMap().value("editorId").toString()));
    }
    for (const auto& mod : control.facegen.loose_mods) {
      if (cancel->load()) return;
      appearance::ProviderFaceGen p;
      p.mod = mod;
      const auto index = snapshot->vfs.find_provider(mod);
      if (!index) {
        result->skipped << QStringLiteral("«%1»: ya no es un mod del perfil").arg(qs(mod));
        in.providers.push_back(std::move(p));
        continue;
      }
      const auto& root = snapshot->vfs.providers()[*index].root;
      QString why;
      if (const auto file = provider_file(root, geometry, &why)) {
        QByteArray bytes;
        if (read_file(*file, kMaxGeometryBytes, &bytes, &why)) {
          p.has_geometry = true;
          p.geometry_file = ss(file_text(*file));
          p.geometry_bytes = bytes.size();
          p.geometry_sha256 = ss(sha256_hex(bytes));
          result->bytes += bytes.size();
          // The studio's safe NIF loader decides whether it is readable (audit F-10/F-11); it never throws.
          const auto info = face::inspect_facegen(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
          p.readable = info.readable;
          if (info.readable && !editor_ids.empty()) {
            bool all = true;
            for (const auto& id : editor_ids) {
              all = all && !id.empty() &&
                    std::any_of(info.shapes.begin(), info.shapes.end(), [&](const auto& s) { return text::iequals_ascii(s.first, id); });
            }
            p.has_record_head_parts = all;
          }
        } else {
          result->skipped << QStringLiteral("«%1» %2: %3").arg(qs(mod), geometry, why);
        }
      } else if (!why.isEmpty()) {
        result->skipped << QStringLiteral("«%1» %2: %3").arg(qs(mod), geometry, why);
      }
      why.clear();
      if (!tint.isEmpty()) {
        if (const auto file = provider_file(root, tint, &why)) {
          QByteArray bytes;
          if (read_file(*file, kMaxTintBytes, &bytes, &why)) {
            p.has_tint = true;
            p.tint_file = ss(file_text(*file));
            p.tint_bytes = bytes.size();
            p.tint_sha256 = ss(sha256_hex(bytes));
            result->bytes += bytes.size();
          } else {
            result->skipped << QStringLiteral("«%1» %2: %3").arg(qs(mod), tint, why);
          }
        } else if (!why.isEmpty()) {
          result->skipped << QStringLiteral("«%1» %2: %3").arg(qs(mod), tint, why);
        }
      }
      in.providers.push_back(std::move(p));
    }
    result->matrix = appearance::build_source_matrix(in);
    result->ms = clock.elapsed();
    QMetaObject::invokeMethod(
        self.data(), [self, serial, result] { if (self) self->on_gathered(serial, result); }, Qt::QueuedConnection);
  });
}

void AppearanceSources::on_gathered(int serial, const std::shared_ptr<Gathered>& result) {
  if (serial != serial_) return;
  QElapsedTimer gui;
  gui.start();
  gathering_ = false;
  gather_ms_ = result->ms;
  read_bytes_ = result->bytes;
  max_gather_ms_ = std::max(max_gather_ms_, result->ms);
  skipped_ = result->skipped;
  diag::record("sources", QStringLiteral("matrix %1: %2 sources, %3 drawable, %4 ms, %5 bytes read")
                              .arg(form_key_).arg(result->matrix.candidates.size()).arg(result->matrix.renderable_count()).arg(result->ms)
                              .arg(result->bytes).toStdString());
  const auto digest = sha256_hex(QByteArray::fromStdString(result->matrix.identity));
  apply_matrix(std::move(result->matrix), digest);
  max_gui_ms_ = std::max(max_gui_ms_, gui.elapsed());
  gui_samples_.push_back(gui.elapsed());
  if (gui_samples_.size() > kMaxGuiSamples) gui_samples_.removeFirst();
}

void AppearanceSources::apply_matrix(appearance::SourceMatrix matrix, const QString& input_digest) {
  matrix_ = std::move(matrix);
  input_digest_ = input_digest;
  QVariantList candidates;
  for (const auto& c : matrix_.candidates) candidates << candidate_map(c);
  matrix_map_ = {{"state", "ready"},
                 {"formKey", form_key_},
                 {"name", name_},
                 {"profileDigest", qs(matrix_.profile_digest)},
                 {"inputDigest", input_digest_},
                 {"gatherMs", gather_ms_},
                 {"readBytes", read_bytes_},
                 {"skipped", skipped_},
                 {"renderable", static_cast<int>(matrix_.renderable_count())},
                 {"candidates", candidates}};
  refresh_draft();
  choose_default_selection();
  emit matrixChanged();
  emit selectionChanged();
  // The two sources on view are drawn only while the A/B dialog is open.
  if (compare_open_) request_compare_images();
}

void AppearanceSources::request_compare_images() {
  if (matrix_map_.value("state").toString() != QLatin1String("ready")) return;
  if (!a_.isEmpty()) requestImage(a_);
  if (!b_.isEmpty()) requestImage(b_);
}

void AppearanceSources::set_compare_open(bool open) {
  if (compare_open_ == open) return;
  compare_open_ = open;
  emit compareOpenChanged();
  if (open) request_compare_images();
}

void AppearanceSources::choose_default_selection() {
  const auto known = [&](const QString& id) { return !id.isEmpty() && matrix_.find(ss(id)) != nullptr; };
  if (known(a_) && (b_.isEmpty() || known(b_))) return;  // the view already shows valid sources
  const auto entry = book_.entries.find(ss(form_key_));
  if (entry != book_.entries.end()) {
    const auto a = qs(entry->second.candidate_a), b = qs(entry->second.candidate_b);
    if (known(a) && (b.isEmpty() || known(b))) {
      a_ = a;
      b_ = b;
      return;
    }
  }
  a_ = matrix_.effective() ? QStringLiteral("effective") : QString();
  b_.clear();
  for (const auto& c : matrix_.candidates) {
    if (!c.effective && c.renderable) {
      b_ = qs(c.id);
      return;
    }
  }
  for (const auto& c : matrix_.candidates) {
    if (!c.effective) {
      b_ = qs(c.id);
      return;
    }
  }
}

bool AppearanceSources::setCompare(const QString& a, const QString& b) {
  if ((!a.isEmpty() && !matrix_.find(ss(a))) || (!b.isEmpty() && !matrix_.find(ss(b)))) return false;
  a_ = a;
  b_ = b;
  emit selectionChanged();
  if (compare_open_) request_compare_images();
  return true;
}

// ------------------------------------------------------------------ pictures

QString AppearanceSources::variant_key(const appearance::SourceCandidate& c) const {
  // Content-addressed: another candidate, other FaceGen files or a new renderer never reuse an image.
  const auto token = sha256_hex(QStringLiteral("%1|%2|%3|%4").arg(qs(c.id), qs(c.geometry_sha256), qs(c.tint_sha256), PortraitRenderer::kRendererVersion).toUtf8());
  return form_key_ + QStringLiteral("@v") + token.left(12);
}

void AppearanceSources::set_image(const QString& id, ImageState s) {
  images_.insert(id, std::move(s));
  ++image_revision_;
  emit imagesChanged();
}

QVariantMap AppearanceSources::imageOf(const QString& id, int revision) const {
  Q_UNUSED(revision)
  const auto* c = matrix_.find(ss(id));
  if (c && c->effective) {
    const auto s = portraits_->stateOf(form_key_, portraits_->revision());
    auto out = s;
    const auto state = s.value("state").toString();
    const auto provisional = s.value("provisionalUrl").toString();
    if (state == QLatin1String("ready")) {
      out["origin"] = s.value("source").toString() == QLatin1String("cache") ? QStringLiteral("cache") : QStringLiteral("studio-render");
    } else if (!provisional.isEmpty()) {
      out["url"] = provisional;
      out["origin"] = QStringLiteral("provisional");
    }
    if (!c->renderable) {
      out["state"] = QStringLiteral("unavailable");
      out["reason"] = qs(c->render_note);
    }
    return out;
  }
  const auto it = images_.constFind(id);
  if (it == images_.cend()) return {{"state", "none"}};
  return image_map(it->state, it->url, it->reason, it->created_at, it->render_ms, it->origin, it->missing);
}

void AppearanceSources::requestImage(const QString& id) {
  const auto* c = matrix_.find(ss(id));
  if (!c) return;
  if (c->effective) {
    if (c->renderable) portraits_->request(form_key_);
    return;
  }
  if (!c->renderable) {
    ImageState s;
    s.state = QStringLiteral("unavailable");
    s.reason = qs(c->render_note);
    set_image(id, s);
    return;
  }
  const auto current = images_.value(id).state;
  if (current == QLatin1String("preparing") || current == QLatin1String("rendering") || current == QLatin1String("ready")) return;
  ++variants_requested_;
  ImageState preparing;
  preparing.state = QStringLiteral("preparing");
  set_image(id, preparing);

  const auto key = variant_key(*c);
  const auto dir = variants_directory();
  if (dir.isEmpty()) {
    ImageState s;
    s.state = QStringLiteral("error");
    s.reason = tr("No hay una carpeta de retratos para este perfil");
    set_image(id, s);
    return;
  }
  struct Wanted {
    QString data_path, file, sha;
  };
  QList<Wanted> wanted;
  for (std::size_t i = 0; i < c->overlay.size(); ++i) {
    wanted.push_back({qs(c->overlay[i].data_path), qs(c->overlay[i].file), qs(i == 0 ? c->geometry_sha256 : c->tint_sha256)});
  }
  const int serial = serial_;
  QThreadPool::globalInstance()->start([self = QPointer<AppearanceSources>(this), snapshot = snapshot_, digest = portraits_->snapshot_digest(), key, dir, id, wanted,
                                        form_key = form_key_, serial] {
    QString error;
    auto overlay = std::make_shared<rendering::AssetOverlay>();
    QHash<QString, QString> files;
    for (const auto& w : wanted) {
      QByteArray bytes;
      QString why;
      if (!read_file(fs::path(w.file.toStdWString()), w.data_path.endsWith(QLatin1String(".nif"), Qt::CaseInsensitive) ? kMaxGeometryBytes : kMaxTintBytes, &bytes, &why)) {
        error = QStringLiteral("%1: %2").arg(w.file, why);
        break;
      }
      // The picture shows exactly the files whose hashes the matrix reported; a file replaced meanwhile is refused.
      if (!w.sha.isEmpty() && sha256_hex(bytes) != w.sha) {
        error = QStringLiteral("El archivo cambió desde que se calculó la matriz (%1); vuelve a seleccionar el NPC").arg(w.file);
        break;
      }
      (*overlay)[text::data_path_key(ss(w.data_path))] = std::make_shared<const std::string>(bytes.constData(), static_cast<std::size_t>(bytes.size()));
      files.insert(key_of(w.data_path), w.file);
    }
    PortraitLookup found;
    if (error.isEmpty()) {
      found = PortraitCache(dir).lookup(key, PortraitRenderer::kRendererVersion, digest, [&](const QString& path) {
        const auto file = files.constFind(key_of(path));
        return file != files.cend() ? PortraitRenderer::fingerprint_of_file(path, *file) : PortraitRenderer::fingerprint_of(snapshot, path);
      });
    }
    QMetaObject::invokeMethod(
        self.data(),
        [self, serial, id, key, dir, digest, error, overlay, files, found, snapshot, form_key] {
          if (!self) return;
          QElapsedTimer gui;
          gui.start();
          if (serial != self->serial_) return;
          if (!error.isEmpty()) {
            ImageState s;
            s.state = QStringLiteral("error");
            s.reason = error;
            ++self->variant_errors_;
            self->set_image(id, s);
            return;
          }
          if (found.state == PortraitCacheState::Valid) {
            ++self->variant_cache_hits_;
            ImageState s;
            s.state = QStringLiteral("ready");
            s.url = QUrl::fromLocalFile(found.image_path).toString();
            s.created_at = found.entry->created_at;
            s.render_ms = found.entry->render_ms;
            s.origin = QStringLiteral("cache");
            s.missing = PortraitRenderer::missing_of(found.entry->resources);
            self->set_image(id, s);
            self->gui_samples_.push_back(gui.elapsed());
            return;
          }
          ImageState rendering;
          rendering.state = QStringLiteral("rendering");
          self->set_image(id, rendering);
          PortraitRenderer::VariantJob job;
          job.base_form_key = form_key;
          job.overlay = overlay;
          for (auto it = files.cbegin(); it != files.cend(); ++it) job.files.insert(it.key(), it.value());
          job.done = [self, serial, id, key, dir, digest, files, snapshot](const QImage& image, const QList<PortraitResource>& resources, qint64 ms,
                                                                          const QString& failure) {
            if (!self) return;
            if (!failure.isEmpty() || image.isNull()) {
              ++self->variant_errors_;
              if (serial == self->serial_) {
                ImageState s;
                s.state = QStringLiteral("error");
                s.reason = failure.isEmpty() ? tr("El render no produjo imagen") : failure;
                self->set_image(id, s);
              }
              return;
            }
            // PNG, cache write and read back run off the GUI thread; only the verdict comes back to it.
            QThreadPool::globalInstance()->start([self, serial, id, key, dir, digest, files, snapshot, image, resources, ms] {
              QString why;
              PortraitLookup back;
              QByteArray png;
              QBuffer buffer(&png);
              buffer.open(QIODevice::WriteOnly);
              if (!image.save(&buffer, "PNG")) why = tr("No se pudo codificar la imagen");
              PortraitCache cache(dir);
              if (why.isEmpty()) {
                QDir().mkpath(dir);
                PortraitEntry entry;
                entry.form_key = key;
                entry.renderer_version = PortraitRenderer::kRendererVersion;
                entry.snapshot_digest = digest;
                entry.resources = resources;
                entry.width = image.width();
                entry.height = image.height();
                entry.render_ms = ms;
                entry.created_at = now_iso();
                QString error_text;
                if (!cache.store(entry, png, &error_text)) why = tr("No se pudo guardar la imagen: %1").arg(error_text);
              }
              if (why.isEmpty()) {
                // Read back through the cache: the pane shows only what the cache confirms (hash and resources).
                back = cache.lookup(key, PortraitRenderer::kRendererVersion, digest, [&](const QString& path) {
                  const auto file = files.constFind(key_of(path));
                  return file != files.cend() ? PortraitRenderer::fingerprint_of_file(path, *file) : PortraitRenderer::fingerprint_of(snapshot, path);
                });
                if (back.state != PortraitCacheState::Valid) why = tr("La caché no confirma la imagen: %1").arg(back.reason);
              }
              QMetaObject::invokeMethod(
                  self.data(),
                  [self, serial, id, ms, why, back] {
                    if (!self) return;
                    QElapsedTimer gui;
                    gui.start();
                    const bool current = serial == self->serial_;
                    if (!why.isEmpty()) {
                      ++self->variant_errors_;
                      if (current) {
                        ImageState s;
                        s.state = QStringLiteral("error");
                        s.reason = why;
                        self->set_image(id, s);
                      }
                    } else {
                      ++self->variants_rendered_;
                      self->total_variant_render_ms_ += ms;
                      self->max_variant_render_ms_ = std::max(self->max_variant_render_ms_, ms);
                      if (current) {
                        ImageState s;
                        s.state = QStringLiteral("ready");
                        s.url = QUrl::fromLocalFile(back.image_path).toString();
                        s.created_at = back.entry->created_at;
                        s.render_ms = ms;
                        s.origin = QStringLiteral("studio-render");
                        s.missing = PortraitRenderer::missing_of(back.entry->resources);
                        self->set_image(id, s);
                      }
                    }
                    self->max_gui_ms_ = std::max(self->max_gui_ms_, gui.elapsed());
                    self->gui_samples_.push_back(gui.elapsed());
                  },
                  Qt::QueuedConnection);
            });
          };
          self->portraits_->submit_variant(key, std::move(job));
          self->gui_samples_.push_back(gui.elapsed());
        },
        Qt::QueuedConnection);
  });
}

// ------------------------------------------------------------------ draft

void AppearanceSources::load_book() {
  const auto dir = qEnvironmentVariableIsSet("SNS_SOURCE_DRAFT_DIR")
                       ? qEnvironmentVariable("SNS_SOURCE_DRAFT_DIR")
                       : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/source-drafts";
  auto file_name = profile_key_;
  file_name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
  book_path_ = QDir(dir).filePath(file_name + ".json");
  book_ = {};
  book_.profile_key = ss(profile_key_);
  book_unreadable_ = false;
  QFile file(book_path_);
  if (!file.exists()) return;
  if (!file.open(QIODevice::ReadOnly)) {
    book_unreadable_ = true;
    return;
  }
  try {
    book_ = appearance::source_draft_from_json(file.readAll().toStdString());
    book_.profile_key = ss(profile_key_);
  } catch (const std::exception&) {
    // Never treat an unreadable draft file as empty and overwrite it.
    book_ = {};
    book_.profile_key = ss(profile_key_);
    book_unreadable_ = true;
  }
}

bool AppearanceSources::save_book(QString* error) {
  if (book_path_.isEmpty()) {
    *error = tr("No hay un perfil cargado");
    return false;
  }
  if (book_unreadable_) {
    *error = tr("el archivo de borradores existente no se puede leer; no se sobrescribe");
    return false;
  }
  QDir().mkpath(QFileInfo(book_path_).absolutePath());
  QSaveFile file(book_path_);
  if (!file.open(QIODevice::WriteOnly)) {
    *error = file.errorString();
    return false;
  }
  const auto json = QByteArray::fromStdString(appearance::source_draft_to_json(book_));
  if (file.write(json) != json.size() || !file.commit()) {
    *error = file.errorString();
    return false;
  }
  return true;
}

void AppearanceSources::refresh_draft() {
  QVariantMap out{{"state", "empty"}, {"path", book_path_}};
  if (book_unreadable_) out["error"] = tr("El archivo de borradores no se puede leer: %1").arg(book_path_);
  if (matrix_map_.value("state").toString() == QLatin1String("ready")) {
    const auto state = appearance::evaluate_draft(book_, matrix_, ss(input_digest_));
    out["state"] = qs(std::string(appearance::to_string(state)));
    if (const auto it = book_.entries.find(matrix_.npc_form_key); it != book_.entries.end()) {
      const auto& e = it->second;
      out["a"] = qs(e.candidate_a);
      out["b"] = qs(e.candidate_b);
      out["chosen"] = qs(e.chosen);
      out["savedAt"] = qs(e.saved_at);
      out["notes"] = qs(e.notes);
    }
  }
  if (out == draft_map_) return;
  draft_map_ = out;
  emit draftChanged();
}

QString AppearanceSources::saveDraft(const QString& chosen, const QString& notes) {
  if (matrix_map_.value("state").toString() != QLatin1String("ready")) return tr("La matriz de fuentes aún no está lista");
  std::string error;
  auto candidate = book_;
  if (!appearance::set_draft(candidate, matrix_, ss(input_digest_), ss(a_), ss(b_), ss(chosen), ss(now_iso()), ss(notes), &error)) return qs(error);
  const auto previous = book_;
  book_ = candidate;
  QString why;
  if (!save_book(&why)) {
    book_ = previous;  // nothing changed on disk or in memory
    return why;
  }
  refresh_draft();
  return {};
}

void AppearanceSources::clearDraft() {
  if (book_.entries.find(ss(form_key_)) == book_.entries.end()) return;
  const auto previous = book_;
  appearance::clear_draft(book_, ss(form_key_));
  QString why;
  if (!save_book(&why)) book_ = previous;
  refresh_draft();
}

// ------------------------------------------------------------------ measurements

QVariantMap AppearanceSources::metrics() const {
  auto samples = gui_samples_;
  std::sort(samples.begin(), samples.end());
  const qint64 p95 = samples.isEmpty() ? 0 : samples[std::min<qsizetype>(samples.size() - 1, static_cast<qsizetype>(samples.size() * 0.95))];
  return {{"gathers", gathers_},
          {"lastGatherMs", gather_ms_},
          {"maxGatherMs", max_gather_ms_},
          {"lastReadBytes", read_bytes_},
          {"variantsRequested", variants_requested_},
          {"variantCacheHits", variant_cache_hits_},
          {"variantsRendered", variants_rendered_},
          {"variantErrors", variant_errors_},
          {"averageVariantRenderMs", variants_rendered_ > 0 ? total_variant_render_ms_ / variants_rendered_ : 0},
          {"maxVariantRenderMs", max_variant_render_ms_},
          {"guiSamples", static_cast<int>(samples.size())},
          {"guiP95Ms", p95},
          {"guiMaxMs", max_gui_ms_}};
}

void AppearanceSources::resetMetrics() {
  gathers_ = variants_requested_ = variant_cache_hits_ = variants_rendered_ = variant_errors_ = 0;
  max_gui_ms_ = total_variant_render_ms_ = max_variant_render_ms_ = max_gather_ms_ = 0;
  gui_samples_.clear();
}

}  // namespace sns::app
