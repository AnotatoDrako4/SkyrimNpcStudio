#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQuickItem>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include <QVector3D>
#include <functional>
#include <optional>

#include "mo2_snapshot.hpp"
#include "portrait_cache.hpp"
#include "sns/rendering/scene.hpp"

class QQuickRenderControl;
class QThread;
class QQuickWindow;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;

namespace sns::app {

class SceneViewModel;

// Portraits of the NPC gallery. One NPC at a time is built by a SceneViewModel of its own (the same NIF,
// DDS and FaceGen code as the main viewer, on its build thread). The stage (PortraitStage.qml: only the
// head, a fixed face camera, neutral lights, no interface) is moved into a window driven by a
// QQuickRenderControl, rendered into a texture of its own and read back, all on the GUI thread. Images go
// to a cache folder of their own and are used again while the profile and the resources they came from
// are unchanged. Cards ask for their portrait when they are created (the visible ones) and release it
// when they are destroyed.
class PortraitRenderer : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  Q_PROPERTY(int revision READ revision NOTIFY changed)
  Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
  Q_PROPERTY(QString cacheDirectory READ cache_directory NOTIFY changed)
  // Settings Center (npc.portraitInstantCache): a card shows the render stored in the cache at once, marked
  // provisional, while it is verified against the profile (or redone when it is out of date).
  Q_PROPERTY(bool instantCache READ instant_cache WRITE set_instant_cache NOTIFY changed)
  // The cache inspector (Settings Center): what every file of this profile's portrait folder is. See inspectCache().
  Q_PROPERTY(QVariantMap cacheReport READ cache_report NOTIFY cacheReportChanged)
  // Progress of redoStale(): {state: "idle" | "running" | "cancelling" | "cancelled" | "done" | "unavailable", reason, total, done, errors,
  //  active, pending, elapsedMs, lastError}.
  Q_PROPERTY(QVariantMap redoProgress READ redo_progress NOTIFY redoProgressChanged)

 public:
  // Bump when the framing, lights, materials or image size change: every cached portrait is redone.
  // v2: framed on the face, not long hair. v3: composed textures record their input files (base head texture).
  static inline const QString kRendererVersion = QStringLiteral("portrait-v3");
  static constexpr int kSize = 256;         // pixels of a portrait
  static constexpr int kMaxQueue = 48;      // requests beyond it (the oldest) are dropped
  static constexpr int kTimeoutMs = 45000;  // one head that never finishes is an error, not a hang
  static constexpr int kCacheHitsPerPass = 8;
  static constexpr int kLookupBatch = 24;   // cache verifications per background task
  static constexpr int kMaxPrefetch = 64;   // NPCs beyond the visible cards prepared while idle
  // The render thread's release at exit, at most: above any real frame (first heads 3.3-4.7 s), only a hang reaches it.
  static constexpr int kReleaseTimeoutMs = kTimeoutMs;

  PortraitRenderer(SceneViewModel* scene, QObject* parent);
  ~PortraitRenderer() override;
  [[nodiscard]] int revision() const { return revision_; }
  [[nodiscard]] QVariantMap stats() const;
  [[nodiscard]] QString cache_directory() const;

  // The QML component of the stage (PortraitStage); it is created on the first portrait, directly inside
  // the off-screen window, so it never shares the main window's Quick 3D resources.
  Q_INVOKABLE void attachStage(QQmlComponent* component);
  Q_INVOKABLE void request(const QString& formKey);
  Q_INVOKABLE void release(const QString& formKey);
  // {state: "none" | "queued" | "rendering" | "ready" | "error", url, reason, source: "cache" | "render",
  // createdAt, renderMs}. `revision` only makes QML bindings re-read it.
  Q_INVOKABLE QVariantMap stateOf(const QString& formKey, int revision = 0) const;
  // Forgets what is known of this NPC and asks again: the cache entry is checked against the profile anew.
  Q_INVOKABLE void refresh(const QString& formKey);
  Q_INVOKABLE void cancelAll();
  // NPCs the user is likely to scroll to next (the rows after the visible ones). They are verified and, when
  // nothing visible is waiting, rendered at low priority; a new call replaces the list. Never on screen by
  // itself: a card still asks with request().
  Q_INVOKABLE void prefetch(const QStringList& formKeys);

  // ---- Variants (the A/B view of the appearance sources, AppearanceSources)
  // One render of an NPC with other files in place of some Data paths (another MO2 mod's FaceGen), drawn by the very
  // same stage, camera and lights as a gallery portrait so two images differ only by what they were made from. It goes
  // ahead of the gallery's renders, never touches the gallery's states or cache folder, and hands the image back to
  // `done` (on the GUI thread) instead of storing it. Read-only: nothing is written to the profile or the game.
  struct VariantJob {
    QString base_form_key;                                   // the NPC to draw
    std::shared_ptr<const rendering::AssetOverlay> overlay;  // consulted before the MO2 files and the BSAs
    QHash<QString, QString> files;                           // lower-case Data path -> absolute file its overlay bytes came from
    // `image` is null and `error` says why when it could not be drawn (cancelled, empty capture, scene failure).
    std::function<void(const QImage& image, const QList<PortraitResource>& resources, qint64 render_ms, const QString& error)> done;
  };
  // `key` identifies the job (it must not look like a gallery NPC key); a job with that key already waiting is replaced.
  void submit_variant(const QString& key, VariantJob job);
  // Drops a job that has not started; one being drawn finishes (its owner ignores it).
  void cancel_variant(const QString& key);
  [[nodiscard]] bool variant_pending(const QString& key) const { return variant_jobs_.contains(key); }
  // The framing the NPC's own head got this session ({center: [x, y, z], distance}), which every variant of it shares;
  // empty when it has not been seen yet. Used by the harness to prove that two sides share one camera.
  Q_INVOKABLE QVariantMap frameOf(const QString& formKey) const;
  // The digest every portrait of this profile is bound to (empty while it is being computed).
  [[nodiscard]] QString snapshot_digest() const { return snapshot_digest_; }
  [[nodiscard]] SnapshotPtr snapshot() const { return snapshot_; }
  // Thread-safe: what one resource is in this snapshot now (used by the background verifications too).
  [[nodiscard]] static PortraitResource fingerprint_of(const SnapshotPtr& snapshot, const QString& path);
  // Thread-safe: a loose file by its absolute path, size and modification time.
  [[nodiscard]] static PortraitResource fingerprint_of_file(const QString& data_path, const QString& absolute_file);
  // What the profile's cache folder holds: {directory, renders, captures, files, bytes, indexed, verified,
  // outdated}. `revision` only makes bindings re-read it.
  Q_INVOKABLE QVariantMap cacheUsage(int revision = 0) const;
  // Removes this profile's studio renders (Skyrim captures attached by the user stay). Empty string or why not.
  Q_INVOKABLE QString clearRenders();
  // ---- Cache inspector. Both run on the thread pool and report through `cacheReport`:
  // {state: "idle" | "running" | "ready" | "unavailable", reason, ms, directory,
  //  counts: {valid, stale, damaged, kept, orphans, unknown, captures}, bytes, unusableRows, unusableBytes,
  //  rows: [{name, formKey, kind, state, code, detail, bytes, createdAt, rendererVersion, changed, unusable}] (at most kMaxReportRows),
  //  truncated, variants: {files, bytes}, lastPurge: {rows, files, bytes, error}}.
  // inspectCache() reads every entry and image (hash) and compares each render with the profile now; it changes nothing.
  Q_INVOKABLE void inspectCache();
  // Removes what cannot be used again (stale or damaged renders, damaged previous renders, images no entry names): never a valid
  // render, a render kept for comparison, a Skyrim capture or a file that is not the studio's. It inspects anew first.
  Q_INVOKABLE void removeUnusable();
  // Removes the renders of the A/B view (AppearanceSources); they are redone when a comparison is opened. Empty string or why not.
  Q_INVOKABLE QString clearVariants();
  // Redoes the portraits the last inspection found stale or damaged, a few at a time, the NPCs the person never scrolled to included.
  // Each one is checked against the profile again first (one that is valid by now is just counted). cancelRedo() stops asking for more;
  // the ones in flight finish. Nothing is deleted: a render replaces its entry and keeps the previous one for comparison.
  Q_INVOKABLE void redoStale();
  Q_INVOKABLE void cancelRedo();
  [[nodiscard]] QVariantMap redo_progress() const { return redo_; }
  static constexpr int kRedoWindow = 4;  // portraits asked for at a time (the render queue holds kMaxQueue)
  [[nodiscard]] QVariantMap cache_report() const { return cache_report_; }
  static constexpr int kMaxReportRows = 3000;
  [[nodiscard]] bool instant_cache() const { return instant_cache_; }
  void set_instant_cache(bool on);
  // What the cache holds for this NPC besides the card portrait, each read back with its hash:
  // {current: {url, createdAt, rendererVersion, resources}, previous: {...} (the render it replaced),
  //  comparable (same renderer: only the resources differ), changedResources: [data paths],
  //  skyrimCapture: {url, createdAt, file, width, height}}. Missing parts are absent.
  Q_INVOKABLE QVariantMap versionsOf(const QString& formKey, int revision = 0) const;
  // A screenshot taken in Skyrim, chosen by the user, kept apart from the studio renders (never a card
  // portrait). Returns an empty string or why it was refused.
  Q_INVOKABLE QString attachGameCapture(const QString& formKey, const QUrl& file);
  Q_INVOKABLE void removeGameCapture(const QString& formKey);
  // A new MO2 snapshot (profile loaded or re-indexed): pending work is cancelled and states forgotten.
  void set_profile(SnapshotPtr snapshot, const QString& profile_key);
  // Data paths a portrait used that the profile does not have (a head drawn without them, e.g. untextured).
  [[nodiscard]] static QStringList missing_of(const QList<PortraitResource>& resources);

 signals:
  void changed();
  void cacheReportChanged();
  void redoProgressChanged();
  void finished(const QString& formKey, const QString& state);

 private:
  void schedule_pump();
  void pump();
  void begin_render(const QString& form_key);
  void on_scene_ready();
  void capture();
  void finish(const QString& state, const QString& reason, const QString& url = {}, const QString& source = {}, const QStringList& missing = {});
  void set_state(const QString& key, const QVariantMap& state);
  // Folder of the portrait caches of every profile (SNS_PORTRAIT_CACHE_DIR or the app's local data).
  [[nodiscard]] static QString portrait_base_directory();
  [[nodiscard]] PortraitResource fingerprint(const QString& path) const;
  void finish_variant(const QImage& image);
  // Framing of a head: where the camera looks and how far it is (PortraitStage.frameHead's rule, in C++). A variant is
  // framed like the NPC's own head so that two FaceGen files of one NPC are seen through one camera.
  struct Frame {
    QVector3D center;
    float distance = 0.0f;
  };
  [[nodiscard]] std::optional<Frame> frame_of_scene() const;
  // The NPC the scene must show for a render key (a gallery key is the NPC itself; a variant key names its NPC).
  [[nodiscard]] QString scene_npc_of(const QString& key) const;
  // Verdict of PortraitCache::lookup() for one NPC in the current profile.
  struct Known {
    bool valid = false;
    QString url, created_at, reason;
    qint64 render_ms = 0;
    QStringList missing;
  };
  enum class Want { Requested, Prefetch, Verify };
  // Cache verifications run on the thread pool, a batch at a time: requested cards first, then the
  // prefetch list, then every render stored in the cache (so a card later asking is ready at once).
  void start_lookups();
  void on_lookups(int serial, quint64 generation, const QList<std::pair<QString, Want>>& batch, const QList<PortraitLookup>& results,
                  qint64 elapsed_ms);
  // A known verdict: the card is ready, or the NPC goes to the render queue (requested ahead of prefetched).
  void route(const QString& form_key, const Known& known, Want want);
  // provisionalUrl / provisionalCreatedAt / provisionalRenderer of the stored render, when the setting is on.
  [[nodiscard]] QVariantMap provisional_of(const QString& key);
  [[nodiscard]] bool is_prefetch(const QString& key) const;
  [[nodiscard]] QList<PortraitResource> resources_of_scene() const;
  // Off-screen rendering: created on the first portrait, then reused.
  bool ensure_offscreen(QString* error);
  // Frames: polish here, sync on the render thread while this thread waits, render and read back there.
  void start_frame(int attempt);
  void on_frame(quint64 serial, int attempt, const QImage& image, const QString& error, qint64 render_ms, qint64 end_ms);
  void complete_capture(const QImage& image, const QString& frame_error, double drawn);
  void release_offscreen();
  void clear_scene();  // scene_->clear(), deferred while a frame is in flight

  SceneViewModel* scene_;
  QPointer<QQmlComponent> stage_component_;
  QPointer<QQuickItem> stage_;
  SnapshotPtr snapshot_;
  QString profile_key_;
  QString snapshot_digest_;  // empty while it is being computed
  int profile_serial_ = 0;
  std::unique_ptr<PortraitCache> cache_;
  QStringList queue_;                   // FormKeys, most recently requested first, waiting for their verdict
  QStringList render_queue_;            // FormKeys to render: requested first, then prefetched
  QStringList prefetch_;                // FormKeys to verify (and render while idle) for the rows ahead
  QSet<QString> prefetch_keys_;         // entry_name of every prefetched NPC not requested by a card
  QHash<QString, PortraitVersion> index_;  // entry_name -> render stored in the cache (provisional display)
  QHash<QString, Known> known_;            // entry_name -> verdict of lookup() for this profile
  bool index_ready_ = false;
  bool lookup_in_flight_ = false;
  quint64 lookup_generation_ = 0;  // a cancel or a new profile drops the verdicts of a batch in flight
  bool instant_cache_ = true;
  QVariantMap cache_report_{{"state", "idle"}};
  void run_inspection(bool purge);  // inspectCache() / removeUnusable()
  void feed_redo();
  void on_redo_finished(const QString& form_key, const QString& state);
  void publish_redo(const QString& state);
  QVariantMap redo_{{"state", "idle"}};
  QStringList redo_pending_;    // FormKeys not asked for yet
  QSet<QString> redo_active_;   // entry_name of the ones asked for and not finished
  int redo_total_ = 0, redo_done_ = 0, redo_errors_ = 0;
  bool redo_cancelled_ = false;
  QString redo_last_error_;
  QElapsedTimer redo_timer_;
  QHash<QString, VariantJob> variant_jobs_;  // waiting or being drawn, by job key (they also sit in render_queue_)
  QHash<QString, Frame> frames_;             // entry_name(NPC) -> framing of its own head, seen this session
  bool probing_ = false;                     // a variant job builds the NPC's own head first, only to learn its framing
  QString current_;                     // FormKey being rendered (or the key of a variant job)
  QHash<QString, QVariantMap> states_;  // PortraitCache::entry_name -> stateOf()
  QTimer watchdog_;
  QElapsedTimer render_timer_;
  bool pump_scheduled_ = false;
  int revision_ = 0;
  std::unique_ptr<QThread> render_thread_;  // sync, render and read back of the portrait window
  QObject* render_worker_ = nullptr;        // lives on render_thread_: target of the calls run there
  bool frame_in_flight_ = false;
  bool clear_after_frame_ = false;  // the head was released while its frame was rendering
  bool shutting_down_ = false;      // after aboutToQuit: no frame, no new render thread
  quint64 frame_serial_ = 0;
  quint64 render_generation_ = 0;  // one per render of a head (begin, cancel, finish): tells renders of one NPC apart
  quint64 frame_generation_ = 0;   // the render the frame in flight belongs to
  QString frame_form_key_;                  // the head the frame in flight belongs to
  QElapsedTimer frame_timer_;
  std::unique_ptr<QQuickRenderControl> render_control_;
  std::unique_ptr<QQuickWindow> offscreen_window_;
  std::unique_ptr<QRhiTexture> texture_;
  std::unique_ptr<QRhiRenderBuffer> depth_;
  std::unique_ptr<QRhiTextureRenderTarget> target_;
  std::unique_ptr<QRhiRenderPassDescriptor> pass_;
  QString offscreen_error_;  // why off-screen rendering is unavailable (then every portrait says so)
  // Measurements (stats()).
  int requested_ = 0, cache_hits_ = 0, rendered_ = 0, errors_ = 0, stale_ = 0, dropped_ = 0, cancelled_ = 0, max_queue_ = 0;
  int second_frames_ = 0;
  int variants_framed_ = 0, variants_self_framed_ = 0;  // variant renders that took the NPC's own framing / framed themselves
  int provisional_ = 0, prefetched_ = 0, lookup_batches_ = 0, verified_ = 0, prefetch_requests_ = 0;
  qint64 max_lookup_ms_ = 0, index_ms_ = 0;
  qint64 total_render_ms_ = 0, max_render_ms_ = 0, total_capture_ms_ = 0;
  qint64 max_capture_ms_ = 0;  // longest block of the GUI thread by a portrait: polish + sync, or PNG + cache
  qint64 max_frame_ms_ = 0;    // longest frame, start to image (render thread; the GUI thread keeps running)
  qint64 max_save_ms_ = 0;     // PNG, hashes, cache write and read back (GUI thread)
  struct { qint64 polish = 0, begin = 0, sync = 0, render = 0, end = 0; } phase_ms_;  // longest of each frame phase
  QString last_stale_reason_, last_error_;
};

}  // namespace sns::app
