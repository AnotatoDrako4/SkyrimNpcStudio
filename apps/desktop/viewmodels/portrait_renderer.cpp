#include "portrait_renderer.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <QSemaphore>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <QUrl>
#include <cstdlib>
#include <rhi/qrhi.h>

#include "diagnostics/crash_diagnostics.hpp"
#include "json_bridge.hpp"
#include "resource_context.hpp"
#include "scene_viewmodel.hpp"
#include "sns/assets/asset_resolver.hpp"

namespace sns::app {
namespace {

// Share of the pixels that differ from the stage background: a capture with almost none drew nothing.
double drawn_share(const QImage& image, const QColor& background) {
  if (image.isNull() || image.width() == 0 || image.height() == 0) return 0.0;
  const auto img = image.convertToFormat(QImage::Format_RGB32);
  const QRgb bg = background.rgb();
  qint64 differ = 0;
  for (int y = 0; y < img.height(); ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      const QRgb p = line[x];
      if (std::abs(qRed(p) - qRed(bg)) + std::abs(qGreen(p) - qGreen(bg)) + std::abs(qBlue(p) - qBlue(bg)) > 24) ++differ;
    }
  }
  return static_cast<double>(differ) / (static_cast<double>(img.width()) * img.height());
}

QString now_iso() {
  const auto now = QDateTime::currentDateTime();
  return now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODateWithMs);
}

}  // namespace

PortraitRenderer::PortraitRenderer(SceneViewModel* scene, QObject* parent) : QObject(parent), scene_(scene) {
  watchdog_.setSingleShot(true);
  watchdog_.setInterval(kTimeoutMs);
  connect(&watchdog_, &QTimer::timeout, this, [this] {
    if (current_.isEmpty()) return;
    scene_->cancel();
    finish(QStringLiteral("error"), tr("Tiempo agotado al construir la cabeza (%1 s)").arg(kTimeoutMs / 1000));
  });
  connect(scene_, &SceneViewModel::ready, this, &PortraitRenderer::on_scene_ready);
  // The render thread ends while the QML engine and the portrait scene still exist (both go before this object
  // at exit), not in the destructor.
  if (QCoreApplication::instance()) {
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
      shutting_down_ = true;  // nothing is rendered (or the renderer rebuilt) any more
      release_offscreen();    // first: it waits for a frame still being rendered
      cancelAll();
    });
  }
}

PortraitRenderer::~PortraitRenderer() { release_offscreen(); }

QVariantMap PortraitRenderer::stats() const {
  return {{"requested", requested_},
          {"cacheHits", cache_hits_},
          {"rendered", rendered_},
          {"errors", errors_},
          {"stale", stale_},
          {"dropped", dropped_},
          {"cancelled", cancelled_},
          {"queue", static_cast<int>(queue_.size())},
          {"maxQueue", max_queue_},
          {"rendering", current_},
          {"totalRenderMs", total_render_ms_},
          {"maxRenderMs", max_render_ms_},
          {"averageRenderMs", rendered_ > 0 ? total_render_ms_ / rendered_ : 0},
          {"totalCaptureMs", total_capture_ms_},
          {"secondFrames", second_frames_},
          {"maxCaptureMs", max_capture_ms_},
          {"maxFrameMs", max_frame_ms_},
          {"maxSaveMs", max_save_ms_},
          {"renderThread", render_thread_ != nullptr},
          {"phases", QVariantMap{{"polishMs", phase_ms_.polish}, {"beginFrameMs", phase_ms_.begin}, {"syncMs", phase_ms_.sync},
                                 {"renderMs", phase_ms_.render}, {"endFrameMs", phase_ms_.end}}},
          {"offscreenError", offscreen_error_},
          {"lastStaleReason", last_stale_reason_},
          {"lastError", last_error_},
          {"ready", stage_component_ && cache_ && !snapshot_digest_.isEmpty()},
          {"instantCache", instant_cache_},
          {"provisional", provisional_},
          {"prefetched", prefetched_},
          {"prefetchQueue", static_cast<int>(prefetch_.size())},
          {"prefetchRequests", prefetch_requests_},
          {"renderQueue", static_cast<int>(render_queue_.size())},
          {"lookupBatches", lookup_batches_},
          {"maxLookupBatchMs", max_lookup_ms_},
          {"verified", verified_},
          {"indexed", static_cast<int>(index_.size())},
          {"indexReady", index_ready_},
          {"indexMs", index_ms_},
          {"rendererVersion", kRendererVersion}};
}

QString PortraitRenderer::portrait_base_directory() {
  return qEnvironmentVariableIsSet("SNS_PORTRAIT_CACHE_DIR") ? qEnvironmentVariable("SNS_PORTRAIT_CACHE_DIR")
                                                             : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/portraits";
}

QString PortraitRenderer::cache_directory() const { return cache_ ? cache_->directory() : QString(); }

void PortraitRenderer::attachStage(QQmlComponent* component) {
  stage_component_ = component;
  schedule_pump();
}

void PortraitRenderer::set_profile(SnapshotPtr snapshot, const QString& profile_key) {
  cancelAll();
  states_.clear();
  snapshot_ = std::move(snapshot);
  profile_key_ = profile_key;
  snapshot_digest_.clear();
  index_.clear();
  known_.clear();
  index_ready_ = false;
  lookup_in_flight_ = false;  // a batch still running belongs to the old profile (serial): dropped on return
  const int serial = ++profile_serial_;
  if (!snapshot_) {  // no profile (being changed): nothing is looked up or rendered until the next one
    cache_.reset();
    ++revision_;
    emit changed();
    return;
  }
  const auto base = portrait_base_directory();
  auto folder = profile_key;
  folder.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
  cache_ = std::make_unique<PortraitCache>(QDir(base).filePath(folder));
  ++revision_;
  emit changed();
  if (!snapshot_) return;
  // While the worker indexes the profile (seconds to minutes) the renders stored in a previous session are read
  // back (hash checked) so the gallery can show them the moment it opens, marked provisional until verified.
  QThreadPool::globalInstance()->start([self = QPointer<PortraitRenderer>(this), dir = cache_->directory(), serial] {
    QElapsedTimer clock;
    clock.start();
    auto index = PortraitCache(dir).scan_current();
    const auto ms = clock.elapsed();
    QMetaObject::invokeMethod(
        self.data(),
        [self, serial, index = std::move(index), ms]() mutable {
          if (!self || serial != self->profile_serial_) return;
          self->index_ = std::move(index);
          self->index_ready_ = true;
          self->index_ms_ = ms;
          diag::record("portrait", QStringLiteral("cache index: %1 stored renders in %2 ms").arg(self->index_.size()).arg(ms).toStdString());
          // Cards that asked before the index was read get their provisional image now.
          for (auto it = self->states_.begin(); it != self->states_.end(); ++it) {
            const auto state = it->value("state").toString();
            if ((state == QLatin1String("queued") || state == QLatin1String("rendering")) && !it->contains("provisionalUrl")) {
              it->insert(self->provisional_of(it.key()));
            }
          }
          ++self->revision_;
          emit self->changed();
          self->schedule_pump();
        },
        Qt::QueuedConnection);
  });
  // What every portrait of this profile depends on besides its own files: the profile and its active
  // plugins (a record edit changes a plugin file). Computed off the GUI thread (hundreds of stats).
  QThreadPool::globalInstance()->start([self = QPointer<PortraitRenderer>(this), snapshot = snapshot_, profile_key, serial] {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(profile_key.toUtf8());
    // Tests only: a salt makes every stored portrait "out of date" as a changed load order would, without
    // touching the MO2 profile (npc-portraits-instant scenario).
    if (qEnvironmentVariableIsSet("SNS_PORTRAIT_DIGEST_SALT")) hash.addData(qEnvironmentVariable("SNS_PORTRAIT_DIGEST_SALT").toUtf8());
    for (const auto& p : snapshot->load_order.active) {
      hash.addData(QByteArray::fromStdString("|" + p.name + "|" + p.provider + "|"));
      if (p.path) {
        const QFileInfo info(QString::fromStdWString(p.path->wstring()));
        hash.addData(QStringLiteral("%1:%2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch()).toUtf8());
      }
    }
    const auto digest = QString::fromLatin1(hash.result().toHex());
    QMetaObject::invokeMethod(
        self.data(),
        [self, serial, digest] {
          if (!self || serial != self->profile_serial_) return;
          self->snapshot_digest_ = digest;
          ++self->revision_;
          emit self->changed();
          self->schedule_pump();
        },
        Qt::QueuedConnection);
  });
}

void PortraitRenderer::request(const QString& formKey) {
  if (formKey.isEmpty()) return;
  ++requested_;
  const auto key = PortraitCache::entry_name(formKey);
  const auto state = states_.value(key).value("state").toString();
  if (state == QLatin1String("ready") || state == QLatin1String("error") || state == QLatin1String("rendering")) return;
  const auto same = [&](const QString& k) { return PortraitCache::entry_name(k) == key; };
  // A card asks for it now: it is no longer only a prefetch, and it goes ahead of the prefetched renders.
  prefetch_keys_.remove(key);
  prefetch_.erase(std::remove_if(prefetch_.begin(), prefetch_.end(), same), prefetch_.end());
  render_queue_.erase(std::remove_if(render_queue_.begin(), render_queue_.end(), same), render_queue_.end());
  queue_.erase(std::remove_if(queue_.begin(), queue_.end(), same), queue_.end());
  queue_.prepend(formKey);
  while (queue_.size() > kMaxQueue) {
    states_.remove(PortraitCache::entry_name(queue_.takeLast()));
    ++dropped_;
  }
  max_queue_ = std::max(max_queue_, static_cast<int>(queue_.size()));
  QVariantMap s{{"state", "queued"}};
  s.insert(provisional_of(key));
  set_state(key, s);
  schedule_pump();
}

void PortraitRenderer::release(const QString& formKey) {
  const auto key = PortraitCache::entry_name(formKey);
  const auto same = [&](const QString& k) { return PortraitCache::entry_name(k) == key; };
  const auto before = queue_.size() + render_queue_.size();
  queue_.erase(std::remove_if(queue_.begin(), queue_.end(), same), queue_.end());
  render_queue_.erase(std::remove_if(render_queue_.begin(), render_queue_.end(), same), render_queue_.end());
  if (queue_.size() + render_queue_.size() != before && states_.value(key).value("state").toString() == QLatin1String("queued")) states_.remove(key);
}

void PortraitRenderer::prefetch(const QStringList& formKeys) {
  ++prefetch_requests_;
  // Prefetched renders that no card asked for and that are not in the new list are dropped from the queue.
  QSet<QString> wanted;
  for (const auto& k : formKeys) wanted.insert(PortraitCache::entry_name(k));
  render_queue_.erase(std::remove_if(render_queue_.begin(), render_queue_.end(),
                                     [&](const QString& k) {
                                       const auto key = PortraitCache::entry_name(k);
                                       if (!prefetch_keys_.contains(key) || wanted.contains(key)) return false;
                                       if (states_.value(key).value("state").toString() == QLatin1String("queued")) states_.remove(key);
                                       return true;
                                     }),
                      render_queue_.end());
  prefetch_.clear();
  prefetch_keys_.clear();
  for (const auto& k : formKeys) {
    if (prefetch_.size() >= kMaxPrefetch) break;
    const auto key = PortraitCache::entry_name(k);
    const auto state = states_.value(key).value("state").toString();
    if (k.isEmpty() || prefetch_keys_.contains(key) || !state.isEmpty()) continue;  // a card already asked, or done
    prefetch_keys_.insert(key);
    prefetch_ << k;
  }
  schedule_pump();
}

bool PortraitRenderer::is_prefetch(const QString& key) const { return prefetch_keys_.contains(key); }

void PortraitRenderer::set_instant_cache(bool on) {
  if (on == instant_cache_) return;
  instant_cache_ = on;
  // Drop or add the provisional image of the cards waiting now.
  for (auto it = states_.begin(); it != states_.end(); ++it) {
    const auto state = it->value("state").toString();
    if (state != QLatin1String("queued") && state != QLatin1String("rendering")) continue;
    it->remove("provisionalUrl");
    it->remove("provisionalCreatedAt");
    it->remove("provisionalRenderer");
    if (on) it->insert(provisional_of(it.key()));
  }
  ++revision_;
  emit changed();
}

QVariantMap PortraitRenderer::provisional_of(const QString& key) {
  if (!instant_cache_) return {};
  const auto it = index_.constFind(key);
  if (it == index_.cend()) return {};
  ++provisional_;
  return {{"provisionalUrl", QUrl::fromLocalFile(it->image_path).toString()},
          {"provisionalCreatedAt", it->entry.created_at},
          {"provisionalRenderer", it->entry.renderer_version}};
}

QVariantMap PortraitRenderer::cacheUsage(int revision) const {
  Q_UNUSED(revision)
  if (!cache_) return {{"available", false}};
  const auto u = cache_->usage();
  int valid = 0, outdated = 0;
  for (const auto& k : known_) (k.valid ? valid : outdated)++;
  return {{"available", true},   {"directory", QDir::toNativeSeparators(cache_->directory())},
          {"renders", u.renders}, {"captures", u.captures},
          {"files", u.files},     {"bytes", u.bytes},
          {"indexed", static_cast<int>(index_.size())},
          {"verified", valid},    {"outdated", outdated}};
}

QString PortraitRenderer::clearRenders() {
  if (!cache_) return tr("No hay un perfil cargado");
  cancelAll();
  QString error;
  const int removed = cache_->clear_renders(&error);
  index_.clear();
  known_.clear();
  states_.clear();
  ++revision_;
  emit changed();
  if (removed < 0) return tr("No se pudieron borrar todos los retratos: %1").arg(error);
  diag::record("portrait", QStringLiteral("cleared %1 cache files in %2").arg(removed).arg(cache_->directory()).toStdString());
  schedule_pump();
  return {};
}

QVariantMap PortraitRenderer::stateOf(const QString& formKey, int revision) const {
  Q_UNUSED(revision)
  const auto it = states_.find(PortraitCache::entry_name(formKey));
  return it == states_.end() ? QVariantMap{{"state", "none"}} : *it;
}

void PortraitRenderer::refresh(const QString& formKey) {
  if (PortraitCache::entry_name(formKey) == PortraitCache::entry_name(current_)) return;
  states_.remove(PortraitCache::entry_name(formKey));
  known_.remove(PortraitCache::entry_name(formKey));  // checked against the profile anew
  request(formKey);
}

void PortraitRenderer::cancelAll() {
  // Everything still waiting (for its verdict or for its render) is forgotten; verdicts already known stay.
  for (auto it = states_.begin(); it != states_.end();) {
    if (it->value("state").toString() == QLatin1String("queued")) {
      it = states_.erase(it);
      ++cancelled_;
    } else {
      ++it;
    }
  }
  queue_.clear();
  render_queue_.clear();
  prefetch_.clear();
  prefetch_keys_.clear();
  ++lookup_generation_;  // a batch being verified now is dropped when it comes back
  ++render_generation_;  // a frame still on the render thread belongs to no render any more
  if (!current_.isEmpty()) {
    watchdog_.stop();
    scene_->cancel();
    states_.remove(PortraitCache::entry_name(current_));
    current_.clear();
    ++cancelled_;
    clear_scene();
  }
  ++revision_;
  emit changed();
}

QVariantMap PortraitRenderer::versionsOf(const QString& formKey, int revision) const {
  Q_UNUSED(revision)
  QVariantMap out;
  if (!cache_ || formKey.isEmpty()) return out;
  const auto describe = [](const PortraitVersion& v) {
    QVariantList resources;
    for (const auto& r : v.entry.resources) {
      resources.push_back(QVariantMap{{"path", r.path}, {"location", r.location}, {"size", r.size}, {"modifiedMs", r.modified_ms}});
    }
    return QVariantMap{{"url", QUrl::fromLocalFile(v.image_path).toString()},
                       {"createdAt", v.entry.created_at},
                       {"rendererVersion", v.entry.renderer_version},
                       {"source", v.entry.source},
                       {"width", v.entry.width},
                       {"height", v.entry.height},
                       {"resources", resources}};
  };
  const auto current = cache_->current(formKey);
  const auto previous = cache_->previous(formKey);
  if (current) out["current"] = describe(*current);
  if (previous) out["previous"] = describe(*previous);
  if (current && previous) {
    // Two renders of the same renderer differ only by what they were made from; across renderers the
    // framing or lights changed too, and the images are not compared as evidence.
    out["comparable"] = current->entry.renderer_version == previous->entry.renderer_version;
    out["changedResources"] = PortraitCache::changed_resources(previous->entry, current->entry);
    out["profileChanged"] = current->entry.snapshot_digest != previous->entry.snapshot_digest;
    out["identicalImage"] = current->entry.image_sha256 == previous->entry.image_sha256;
  }
  if (const auto capture = cache_->game_capture(formKey)) {
    auto c = describe(*capture);
    c["file"] = capture->entry.resources.isEmpty() ? QString() : capture->entry.resources.front().location.mid(QStringLiteral("user-file:").size());
    out["skyrimCapture"] = c;
  }
  return out;
}

QString PortraitRenderer::attachGameCapture(const QString& formKey, const QUrl& file) {
  if (!cache_) return tr("No hay un perfil cargado");
  if (formKey.isEmpty()) return tr("Ningún NPC seleccionado");
  QString error;
  if (!cache_->store_game_capture(formKey, file.isLocalFile() ? file.toLocalFile() : file.toString(), now_iso(), &error)) {
    return tr("Captura no adjuntada: %1").arg(error);
  }
  diag::record("portrait", ("skyrim capture attached to " + formKey).toStdString());
  ++revision_;
  emit changed();
  return {};
}

void PortraitRenderer::removeGameCapture(const QString& formKey) {
  if (!cache_ || !cache_->remove_game_capture(formKey)) return;
  ++revision_;
  emit changed();
}

void PortraitRenderer::schedule_pump() {
  if (pump_scheduled_) return;
  pump_scheduled_ = true;
  QTimer::singleShot(0, this, [this] { pump(); });
}

void PortraitRenderer::clear_scene() {
  // Conservative: the head a frame is rendering is not deleted until that frame is back (the render thread
  // reads the scene graph built from it during render()).
  if (frame_in_flight_) {
    clear_after_frame_ = true;
    return;
  }
  scene_->clear();
}

void PortraitRenderer::pump() {
  pump_scheduled_ = false;
  if (shutting_down_) return;
  if (cache_ && snapshot_ && !snapshot_digest_.isEmpty()) {
    // Requested cards whose verdict is already known are resolved now (ready, or to the render queue).
    QList<std::pair<QString, Known>> resolved;
    for (auto it = queue_.begin(); it != queue_.end();) {
      const auto k = known_.constFind(PortraitCache::entry_name(*it));
      if (k == known_.cend()) { ++it; continue; }
      resolved.push_back({*it, *k});
      it = queue_.erase(it);
    }
    for (const auto& [form_key, known] : resolved) route(form_key, known, Want::Requested);
    start_lookups();
  }
  if (frame_in_flight_) {
    // The next head is built once the frame in flight is back (on_frame pumps). A frame that never comes back
    // (GPU hang, device lost) fails the waiting heads with that cause instead of leaving them queued.
    if (frame_timer_.elapsed() > kTimeoutMs) {
      if (offscreen_error_.isEmpty()) offscreen_error_ = tr("el hilo de render no terminó un fotograma en %1 s").arg(kTimeoutMs / 1000);
      for (const auto& list : {queue_, render_queue_}) {
        for (const auto& k : list) {
          set_state(PortraitCache::entry_name(k), {{"state", "error"}, {"reason", tr("Render fuera de pantalla no disponible: %1").arg(offscreen_error_)}});
          ++errors_;
        }
      }
      last_error_ = offscreen_error_;
      queue_.clear();
      render_queue_.clear();
      return;
    }
    QTimer::singleShot(250, this, [this] { schedule_pump(); });
    return;
  }
  if (!current_.isEmpty() || !stage_component_ || !cache_ || !snapshot_ || snapshot_digest_.isEmpty()) return;
  if (!render_queue_.isEmpty()) {
    const auto form_key = render_queue_.takeFirst();
    const auto key = PortraitCache::entry_name(form_key);
    const bool prefetched = prefetch_keys_.contains(key);
    begin_render(form_key);
    if (prefetched) {
      prefetch_keys_.remove(key);
      ++prefetched_;
    }
  }
}

void PortraitRenderer::start_lookups() {
  if (lookup_in_flight_ || !cache_ || !snapshot_ || snapshot_digest_.isEmpty()) return;
  // Requested cards first, then the rows ahead (prefetch), then every stored render not verified yet.
  QList<std::pair<QString, Want>> batch;
  QSet<QString> taken;
  const auto add = [&](const QString& form_key, Want want) {
    const auto key = PortraitCache::entry_name(form_key);
    if (batch.size() >= kLookupBatch || taken.contains(key) || known_.contains(key)) return;
    taken.insert(key);
    batch.push_back({form_key, want});
  };
  for (const auto& k : std::as_const(queue_)) add(k, Want::Requested);
  for (const auto& k : std::as_const(prefetch_)) add(k, Want::Prefetch);
  if (batch.size() < kLookupBatch && index_ready_) {
    for (auto it = index_.cbegin(); it != index_.cend() && batch.size() < kLookupBatch; ++it) add(it->entry.form_key, Want::Verify);
  }
  if (batch.isEmpty()) return;
  // Requested ones leave the waiting list now (their verdict routes them); prefetched ones too.
  for (const auto& [form_key, want] : batch) {
    const auto key = PortraitCache::entry_name(form_key);
    const auto same = [&](const QString& k) { return PortraitCache::entry_name(k) == key; };
    if (want == Want::Requested) queue_.erase(std::remove_if(queue_.begin(), queue_.end(), same), queue_.end());
    if (want == Want::Prefetch) prefetch_.erase(std::remove_if(prefetch_.begin(), prefetch_.end(), same), prefetch_.end());
  }
  lookup_in_flight_ = true;
  ++lookup_batches_;
  QThreadPool::globalInstance()->start([self = QPointer<PortraitRenderer>(this), batch, dir = cache_->directory(), digest = snapshot_digest_,
                                        snapshot = snapshot_, serial = profile_serial_, generation = lookup_generation_] {
    QElapsedTimer clock;
    clock.start();
    const PortraitCache cache(dir);
    QList<PortraitLookup> results;
    results.reserve(batch.size());
    for (const auto& item : batch) {
      results.push_back(cache.lookup(item.first, kRendererVersion, digest,
                                     [&snapshot](const QString& path) { return fingerprint_of(snapshot, path); }));
    }
    const auto ms = clock.elapsed();
    QMetaObject::invokeMethod(
        self.data(), [self, serial, generation, batch, results, ms] { if (self) self->on_lookups(serial, generation, batch, results, ms); },
        Qt::QueuedConnection);
  });
}

void PortraitRenderer::on_lookups(int serial, quint64 generation, const QList<std::pair<QString, Want>>& batch,
                                  const QList<PortraitLookup>& results, qint64 elapsed_ms) {
  if (serial != profile_serial_) return;  // another profile: set_profile already reset everything
  lookup_in_flight_ = false;
  max_lookup_ms_ = std::max(max_lookup_ms_, elapsed_ms);
  if (generation != lookup_generation_) {  // cancelled meanwhile: these verdicts are not used
    schedule_pump();
    return;
  }
  for (qsizetype i = 0; i < batch.size() && i < results.size(); ++i) {
    const auto& [form_key, want] = batch[i];
    const auto& found = results[i];
    const auto key = PortraitCache::entry_name(form_key);
    Known k;
    if (found.state == PortraitCacheState::Valid) {
      k.valid = true;
      k.url = QUrl::fromLocalFile(found.image_path).toString();
      k.created_at = found.entry->created_at;
      k.render_ms = found.entry->render_ms;
      k.missing = missing_of(found.entry->resources);
    } else {
      k.reason = found.state == PortraitCacheState::Missing ? QStringLiteral("not in the cache") : found.reason;
      if (found.state == PortraitCacheState::Stale) {
        ++stale_;
        last_stale_reason_ = form_key + ": " + found.reason;
      }
    }
    known_.insert(key, k);
    ++verified_;
    const auto state = states_.value(key).value("state").toString();
    if (want == Want::Requested && state == QLatin1String("queued")) route(form_key, k, Want::Requested);
    else if (want == Want::Prefetch && prefetch_keys_.contains(key) && state.isEmpty()) route(form_key, k, Want::Prefetch);
    else if (want != Want::Verify && state == QLatin1String("queued")) route(form_key, k, Want::Requested);  // a card asked meanwhile
  }
  schedule_pump();
}

void PortraitRenderer::route(const QString& form_key, const Known& known, Want want) {
  const auto key = PortraitCache::entry_name(form_key);
  if (known.valid) {
    ++cache_hits_;
    if (want == Want::Prefetch) prefetch_keys_.remove(key);
    QVariantMap s{{"state", "ready"}, {"url", known.url}, {"source", "cache"}, {"createdAt", known.created_at}, {"renderMs", known.render_ms}};
    if (!known.missing.isEmpty()) s["missing"] = known.missing;
    set_state(key, s);
    emit finished(form_key, QStringLiteral("ready"));
    return;
  }
  const auto same = [&](const QString& k) { return PortraitCache::entry_name(k) == key; };
  render_queue_.erase(std::remove_if(render_queue_.begin(), render_queue_.end(), same), render_queue_.end());
  // A requested card goes first (most recent first, like the requests); a prefetched NPC waits at the end.
  if (want == Want::Prefetch) render_queue_.append(form_key);
  else render_queue_.prepend(form_key);
  while (render_queue_.size() > kMaxQueue) {  // the oldest prefetched render goes first, then the oldest request
    auto victim = std::find_if(render_queue_.rbegin(), render_queue_.rend(), [&](const QString& k) { return is_prefetch(PortraitCache::entry_name(k)); });
    const auto gone = victim != render_queue_.rend() ? *victim : render_queue_.last();
    render_queue_.removeOne(gone);
    const auto gone_key = PortraitCache::entry_name(gone);
    prefetch_keys_.remove(gone_key);
    if (states_.value(gone_key).value("state").toString() == QLatin1String("queued")) states_.remove(gone_key);
    ++dropped_;
  }
  QVariantMap s{{"state", "queued"}};
  if (want == Want::Prefetch) {
    states_.insert(key, s);  // no card shows it: nothing to repaint
    return;
  }
  s.insert(provisional_of(key));
  set_state(key, s);
}

void PortraitRenderer::begin_render(const QString& form_key) {
  ++render_generation_;
  current_ = form_key;
  {
    const auto key = PortraitCache::entry_name(form_key);
    QVariantMap s{{"state", "rendering"}};
    // The stored render stays on the card (marked provisional) until the new one replaces it.
    const auto before = states_.value(key);
    for (const auto* field : {"provisionalUrl", "provisionalCreatedAt", "provisionalRenderer"}) {
      if (before.contains(QLatin1String(field))) s.insert(QLatin1String(field), before.value(QLatin1String(field)));
    }
    if (!s.contains("provisionalUrl") && !prefetch_keys_.contains(key)) s.insert(provisional_of(key));
    set_state(key, s);
  }
  diag::record("portrait", ("render " + form_key).toStdString());
  render_timer_.start();
  watchdog_.start();
  scene_->set_npc_override(form_key);  // builds on the scene's own thread; ready() follows
}

void PortraitRenderer::on_scene_ready() {
  if (current_.isEmpty() || scene_->npc_override() != current_) return;
  for (const auto& issue : scene_->issues()) {
    const auto m = issue.toMap();
    const auto code = m.value("code").toString();
    if (code == QLatin1String("SceneFailed") || code == QLatin1String("AppearanceUnavailable")) {
      finish(QStringLiteral("error"), m.value("message").toString());
      return;
    }
  }
  bool head = false;
  for (const auto& p : scene_->parts()) {
    const auto m = p.toMap();
    head = head || (m.value("group").toString() == QLatin1String("head") && m.value("shownByDefault").toBool());
  }
  if (!head) {
    finish(QStringLiteral("error"), tr("Sin cabeza que mostrar: el NPC no tiene FaceGen ni piezas de cabeza visibles"));
    return;
  }
  QString error;
  if (!ensure_offscreen(&error)) {
    finish(QStringLiteral("error"), tr("Render fuera de pantalla no disponible: %1").arg(error));
    return;
  }
  QVariant framed;
  if (!stage_ || !QMetaObject::invokeMethod(stage_, "frameHead", Q_RETURN_ARG(QVariant, framed)) || !framed.toBool()) {
    finish(QStringLiteral("error"), tr("El escenario de retratos no pudo encuadrar la cabeza"));
    return;
  }
  // One pass of the event loop: the stage has applied the new parts and the camera.
  QTimer::singleShot(0, this, [this, form_key = current_] {
    if (current_ == form_key) capture();
  });
}

void PortraitRenderer::capture() {
  if (!stage_) {
    finish(QStringLiteral("error"), tr("Sin escenario de retratos"));
    return;
  }
  QString error;
  if (!ensure_offscreen(&error)) {
    finish(QStringLiteral("error"), tr("Render fuera de pantalla no disponible: %1").arg(error));
    return;
  }
  if (frame_in_flight_) {  // the render thread still finishes a frame of a cancelled head
    if (frame_timer_.elapsed() > kTimeoutMs) {
      // That frame never came back (GPU hang, device lost): say so, instead of every head timing out.
      offscreen_error_ = tr("el hilo de render no terminó un fotograma en %1 s").arg(kTimeoutMs / 1000);
      finish(QStringLiteral("error"), tr("Render fuera de pantalla no disponible: %1").arg(offscreen_error_));
      return;
    }
    QTimer::singleShot(10, this, [this, form_key = current_] {
      if (current_ == form_key) capture();
    });
    return;
  }
  frame_form_key_ = current_;
  frame_generation_ = render_generation_;
  frame_timer_.start();
  start_frame(1);
}

void PortraitRenderer::start_frame(int attempt) {
  // GUI thread: polish, then the render thread synchronises the scene while this thread waits (Qt's rule for
  // a threaded render control). render(), the read back and endFrame() run there without blocking: the GUI
  // thread only loses the polish and the sync.
  QElapsedTimer gui;
  gui.start();
  QElapsedTimer phase;
  phase.start();
  render_control_->polishItems();
  phase_ms_.polish = std::max(phase_ms_.polish, phase.elapsed());
  qint64 begin_ms = 0, sync_ms = 0;
  QMetaObject::invokeMethod(
      render_worker_,
      [this, &begin_ms, &sync_ms] {
        QElapsedTimer t;
        t.start();
        render_control_->beginFrame();
        begin_ms = t.restart();
        render_control_->sync();
        sync_ms = t.elapsed();
      },
      Qt::BlockingQueuedConnection);
  phase_ms_.begin = std::max(phase_ms_.begin, begin_ms);
  phase_ms_.sync = std::max(phase_ms_.sync, sync_ms);
  max_capture_ms_ = std::max(max_capture_ms_, gui.elapsed());
  frame_in_flight_ = true;
  const auto serial = ++frame_serial_;
  // The frame works on copies of these pointers, never on the members: release_offscreen() moves the RHI
  // resources out on this thread (and frees them on the render thread, after this frame). Reading texture_
  // there raced with that move: a null texture is read back as a swap chain (D3D11 assert at quit).
  QQuickRenderControl* control = render_control_.get();
  QRhiTexture* texture = texture_.get();
  QPointer<PortraitRenderer> self(this);  // made here, dereferenced only back on this thread
  QMetaObject::invokeMethod(
      render_worker_,
      [self, control, texture, serial, attempt] {
        QElapsedTimer t;
        t.start();
        control->render();
        const qint64 render_ms = t.restart();
        QRhi* rhi = control->rhi();
        QRhiReadbackResult result;
        bool done = false;
        result.completed = [&done] { done = true; };
        QString error;
        if (QRhiCommandBuffer* commands = control->commandBuffer()) {
          QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
          batch->readBackTexture(QRhiReadbackDescription(texture), &result);
          commands->resourceUpdate(batch);
        } else {
          error = QStringLiteral("no command buffer (beginFrame failed)");
        }
        control->endFrame();  // an offscreen frame: waits for the GPU, the read back is complete
        const qint64 end_ms = t.elapsed();
        QImage image;
        if (!error.isEmpty()) {
        } else if (done) {
          const QImage wrapped(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(), result.pixelSize.height(),
                               QImage::Format_RGBA8888_Premultiplied);
          image = rhi->isYUpInFramebuffer() ? wrapped.flipped(Qt::Vertical) : wrapped.copy();
        } else {
          error = QStringLiteral("texture readback did not complete");
        }
        // Back to the GUI thread through the application object: the renderer may be gone by then.
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, serial, attempt, image, error, render_ms, end_ms] {
              if (self) self->on_frame(serial, attempt, image, error, render_ms, end_ms);
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void PortraitRenderer::on_frame(quint64 serial, int attempt, const QImage& image, const QString& frame_error, qint64 render_ms, qint64 end_ms) {
  if (serial != frame_serial_) return;
  frame_in_flight_ = false;
  if (clear_after_frame_) {
    clear_after_frame_ = false;
    scene_->clear();
  }
  phase_ms_.render = std::max(phase_ms_.render, render_ms);
  phase_ms_.end = std::max(phase_ms_.end, end_ms);
  // Cancelled meanwhile (even if the same NPC is being rendered again: another generation, other resources).
  if (frame_generation_ != render_generation_ || current_.isEmpty() || current_ != frame_form_key_) {
    schedule_pump();
    return;
  }
  const auto background = stage_->property("background").value<QColor>();
  const double drawn = frame_error.isEmpty() ? drawn_share(image, background) : 0.0;
  if (frame_error.isEmpty() && drawn < 0.02 && attempt == 1) {
    // A head's first frame may still lack uploads: one more frame before calling it empty.
    ++second_frames_;
    start_frame(2);
    return;
  }
  total_capture_ms_ += frame_timer_.elapsed();
  max_frame_ms_ = std::max(max_frame_ms_, frame_timer_.elapsed());
  QElapsedTimer gui;  // PNG, cache write and read back: on the GUI thread
  gui.start();
  complete_capture(image, frame_error, drawn);
  max_save_ms_ = std::max(max_save_ms_, gui.elapsed());
  max_capture_ms_ = std::max(max_capture_ms_, gui.elapsed());
}

void PortraitRenderer::complete_capture(const QImage& image, const QString& frame_error, double drawn) {
  QString error;
  if (!frame_error.isEmpty()) {
    finish(QStringLiteral("error"), tr("El render fuera de pantalla falló: %1").arg(frame_error));
    return;
  }
  if (drawn < 0.02) {
    finish(QStringLiteral("error"), tr("La captura salió vacía (%1 % de píxeles dibujados)").arg(drawn * 100, 0, 'f', 1));
    return;
  }
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "PNG")) {
    finish(QStringLiteral("error"), tr("No se pudo codificar el retrato"));
    return;
  }
  PortraitEntry entry;
  entry.form_key = current_;
  entry.renderer_version = kRendererVersion;
  entry.snapshot_digest = snapshot_digest_;
  entry.resources = resources_of_scene();
  entry.width = image.width();
  entry.height = image.height();
  entry.render_ms = render_timer_.elapsed();
  entry.created_at = now_iso();
  if (!cache_->store(entry, png, &error)) {
    finish(QStringLiteral("error"), tr("No se pudo guardar el retrato: %1").arg(error));
    return;
  }
  // Read back through the cache: the card shows only what the cache confirms.
  const auto back = cache_->lookup(current_, kRendererVersion, snapshot_digest_, [this](const QString& path) { return fingerprint(path); });
  if (back.state != PortraitCacheState::Valid) {
    finish(QStringLiteral("error"), tr("La caché no confirma el retrato: %1").arg(back.reason));
    return;
  }
  // The new render is what the gallery shows at once from now on (and in the next session).
  if (auto stored = cache_->current(current_)) index_.insert(PortraitCache::entry_name(current_), std::move(*stored));
  known_.insert(PortraitCache::entry_name(current_),
                Known{true, QUrl::fromLocalFile(back.image_path).toString(), back.entry->created_at, {}, entry.render_ms, missing_of(back.entry->resources)});
  ++rendered_;
  total_render_ms_ += entry.render_ms;
  max_render_ms_ = std::max(max_render_ms_, entry.render_ms);
  finish(QStringLiteral("ready"), {}, QUrl::fromLocalFile(back.image_path).toString(), QStringLiteral("render"),
         missing_of(back.entry->resources));
}

void PortraitRenderer::finish(const QString& state, const QString& reason, const QString& url, const QString& source,
                              const QStringList& missing) {
  watchdog_.stop();
  const auto form_key = current_;
  QVariantMap s{{"state", state}};
  if (!reason.isEmpty()) s["reason"] = reason;
  if (!url.isEmpty()) {
    s["url"] = url;
    s["source"] = source;
    s["createdAt"] = now_iso();
    s["renderMs"] = render_timer_.elapsed();
  }
  if (!missing.isEmpty()) s["missing"] = missing;
  if (state == QLatin1String("error")) {
    ++errors_;
    last_error_ = form_key + ": " + reason;
    diag::record("portrait", ("error " + form_key + ": " + reason).toStdString());
  }
  current_.clear();
  ++render_generation_;
  clear_scene();  // releases this head's objects before the next one
  set_state(PortraitCache::entry_name(form_key), s);
  emit finished(form_key, state);
  schedule_pump();
}

void PortraitRenderer::set_state(const QString& key, const QVariantMap& state) {
  states_.insert(key, state);
  ++revision_;
  emit changed();
}

bool PortraitRenderer::ensure_offscreen(QString* error) {
  if (!offscreen_error_.isEmpty()) {
    *error = offscreen_error_;
    return false;
  }
  if (shutting_down_) {
    *error = QStringLiteral("closing");
    return false;
  }
  if (target_) return true;
  const auto fail = [&](const QString& why) {
    offscreen_error_ = why;
    *error = why;
    release_offscreen();
    return false;
  };
  render_control_ = std::make_unique<QQuickRenderControl>();
  offscreen_window_ = std::make_unique<QQuickWindow>(render_control_.get());
  offscreen_window_->setGeometry(0, 0, kSize, kSize);
  if (!stage_component_) return fail(QStringLiteral("no stage component"));
  // Created without a parent item, then placed straight into the off-screen window (never the main one).
  // create() alone would use the engine's root context, where the ids of the file that declares the
  // component (Main.qml's `window`) do not exist: the stage would get no scene and frame nothing. The
  // scene is handed over directly, in the component's own context.
  auto* object = stage_component_->createWithInitialProperties({{QStringLiteral("scene"), QVariant::fromValue(scene_)}},
                                                               stage_component_->creationContext());
  stage_ = qobject_cast<QQuickItem*>(object);
  if (!stage_) {
    delete object;
    return fail(QStringLiteral("stage not created: %1").arg(stage_component_->errorString()));
  }
  stage_->setParent(offscreen_window_->contentItem());  // deleted with the window
  stage_->setParentItem(offscreen_window_->contentItem());
  offscreen_window_->setColor(stage_->property("background").value<QColor>());
  // Rendering happens on a thread of its own (measured on the GUI thread: render() 3.4-4.7 s for the first
  // heads of a run, Qt Quick 3D generating its materials). Qt's rules for a threaded render control:
  // prepareThread() first, then initialize(), sync(), render() and the QRhi resources on that thread;
  // polishItems() on the GUI thread, which waits during sync().
  render_thread_ = std::make_unique<QThread>();
  render_thread_->setObjectName(QStringLiteral("PortraitRender"));
  render_thread_->start();
  render_worker_ = new QObject;
  render_worker_->moveToThread(render_thread_.get());
  render_control_->prepareThread(render_thread_.get());
  QString why;
  QString backend;
  QMetaObject::invokeMethod(
      render_worker_,
      [this, &why, &backend] {
        if (!render_control_->initialize()) {
          why = QStringLiteral("QQuickRenderControl::initialize");
          return;
        }
        QRhi* rhi = render_control_->rhi();
        if (!rhi) {
          why = QStringLiteral("no QRhi");
          return;
        }
        backend = QString::fromLatin1(rhi->backendName());
        const QSize size(kSize, kSize);
        texture_.reset(rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!texture_->create()) {
          why = QStringLiteral("render texture");
          return;
        }
        depth_.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
        if (!depth_->create()) {
          why = QStringLiteral("depth buffer");
          return;
        }
        QRhiTextureRenderTargetDescription description((QRhiColorAttachment(texture_.get())));
        description.setDepthStencilBuffer(depth_.get());
        target_.reset(rhi->newTextureRenderTarget(description));
        pass_.reset(target_->newCompatibleRenderPassDescriptor());
        target_->setRenderPassDescriptor(pass_.get());
        if (!target_->create()) why = QStringLiteral("render target");
      },
      Qt::BlockingQueuedConnection);
  if (!why.isEmpty()) return fail(why);
  offscreen_window_->setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target_.get()));
  stage_->setSize(QSizeF(kSize, kSize));
  stage_->setVisible(true);
  diag::record("portrait", "off-screen rendering ready on its own thread (" + backend.toStdString() + ")");
  return true;
}

void PortraitRenderer::release_offscreen() {
  // On the render thread: its QRhi resources and the scene graph (invalidate). Queued behind a frame still
  // being rendered, so that frame completes first. Then the window (with the stage) and the render control
  // here, and the thread ends.
  if (render_thread_ && render_worker_) {
    // The release does not touch this object: if it times out (a frame stuck on the GPU), the thread, the
    // render control and the window are left alive instead of blocking the exit forever.
    struct Resources {
      std::unique_ptr<QRhiTexture> texture;
      std::unique_ptr<QRhiRenderBuffer> depth;
      std::unique_ptr<QRhiTextureRenderTarget> target;
      std::unique_ptr<QRhiRenderPassDescriptor> pass;
    };
    auto resources = std::make_shared<Resources>(Resources{std::move(texture_), std::move(depth_), std::move(target_), std::move(pass_)});
    auto released = std::make_shared<QSemaphore>();
    auto abandoned = std::make_shared<std::atomic<bool>>(false);
    QQuickRenderControl* control = render_control_.get();
    QMetaObject::invokeMethod(
        render_worker_,
        [resources, released, abandoned, control] {
          if (abandoned->load()) return;  // the GUI thread gave up waiting: its window and items are not ours
          resources->pass.reset();
          resources->target.reset();
          resources->depth.reset();
          resources->texture.reset();
          if (control) control->invalidate();
          released->release();
        },
        Qt::QueuedConnection);
    if (!released->tryAcquire(1, kReleaseTimeoutMs)) {
      abandoned->store(true);
      diag::record("portrait", "render thread did not release in time: its objects are left alive");
      (void)render_control_.release();
      (void)offscreen_window_.release();
      (void)render_thread_.release();
      render_worker_ = nullptr;
      ++frame_serial_;  // its result, if it ever comes, is dropped; the frame stays "in flight": the head is kept
      return;
    }
  }
  frame_in_flight_ = false;
  ++frame_serial_;  // a frame result still queued for this thread is dropped
  if (clear_after_frame_) {
    clear_after_frame_ = false;
    scene_->clear();
  }
  offscreen_window_.reset();
  render_control_.reset();
  if (render_thread_) {
    if (render_worker_) render_worker_->deleteLater();
    render_worker_ = nullptr;
    render_thread_->quit();
    render_thread_->wait();
    render_thread_.reset();
  }
}

PortraitResource PortraitRenderer::fingerprint(const QString& path) const { return fingerprint_of(snapshot_, path); }

PortraitResource PortraitRenderer::fingerprint_of(const SnapshotPtr& snapshot, const QString& path) {
  PortraitResource r{path, QStringLiteral("missing"), -1, -1};
  if (!snapshot || path.isEmpty()) return r;
  const auto data = ss(path);
  if (const auto file = snapshot->vfs.resolve(data)) {
    const QFileInfo info(QString::fromStdWString(file->absolute.wstring()));
    r.location = QStringLiteral("loose:") + info.absoluteFilePath();
    r.size = info.size();
    r.modified_ms = info.lastModified().toMSecsSinceEpoch();
    return r;
  }
  if (const auto ctx = ResourceContext::for_snapshot(snapshot); ctx && ctx->resolver) {
    if (const auto loc = ctx->resolver->locate(data)) r.location = QStringLiteral("archive:%1@%2").arg(qs(loc->provider), qs(loc->archive_provider));
  }
  return r;
}

QList<PortraitResource> PortraitRenderer::resources_of_scene() const {
  QList<PortraitResource> out;
  QSet<QString> seen;
  auto add = [&](const QString& path) {
    if (path.isEmpty() || seen.contains(path.toLower())) return;
    seen.insert(path.toLower());
    out.push_back(fingerprint(path));
  };
  // Composed textures are named by their inputs: "<diffuse>|facetint:<tint>", "<diffuse>|skintint:<colour>",
  // "<normal map>|tangent:<mesh>|<shape>". Each input file is recorded (a changed base head texture must make
  // the portrait stale); colours and shape names are not files.
  auto add_key = [&](const QString& key) {
    for (auto part : key.split(QLatin1Char('|'))) {
      const auto colon = part.indexOf(QLatin1Char(':'));
      if (colon > 1 && colon < 12 && !part.left(colon).contains(QLatin1Char('\\'))) part = part.mid(colon + 1);
      if ((part.contains(QLatin1Char('\\')) || part.contains(QLatin1Char('/'))) && part.contains(QLatin1Char('.'))) add(part);
    }
  };
  for (const auto& p : scene_->parts()) {
    const auto m = p.toMap();
    if (m.value("group").toString() != QLatin1String("head")) continue;
    add_key(m.value("path").toString());
    for (const auto& t : m.value("inspector").toMap().value("textures").toList()) add_key(t.toMap().value("path").toString());
  }
  return out;
}

QStringList PortraitRenderer::missing_of(const QList<PortraitResource>& resources) {
  QStringList out;
  for (const auto& r : resources) {
    if (r.location == QLatin1String("missing")) out << r.path;
  }
  return out;
}

}  // namespace sns::app
