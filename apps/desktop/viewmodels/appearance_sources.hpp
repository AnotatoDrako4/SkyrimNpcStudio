#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

#include "mo2_snapshot.hpp"
#include "portrait_renderer.hpp"
#include "sns/appearance/control_map.hpp"
#include "sns/appearance/face_source_index.hpp"
#include "sns/appearance/source_draft.hpp"
#include "sns/appearance/source_matrix.hpp"

namespace sns::app {

// The Appearance Source Matrix of the selected NPC and its A/B view (sns::appearance::build_source_matrix): every
// MO2 mod that ships a loose FaceGen for the NPC next to the one the game loads, each judged by the control map's own
// rules, and a render of each drawn by the portrait renderer's stage with that mod's files in place of the winner's
// (an overlay, in memory). Read-only end to end: nothing is written to the profile, MO2, the game, a project or a
// plugin; the only file it writes is the user's draft (which source to look at / prefer), in the studio's own folder.
// The picture is the editor's approximation, never a capture made inside Skyrim.
class AppearanceSources : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  // {state: "none" | "gathering" | "ready", formKey, name, profileDigest, inputDigest, gatherMs, readBytes, skipped: [...],
  //  renderable, candidates: [{id, label, effective, status, recordPlugin, recordMod, facegenMod, facetintMod, geometryPath,
  //  tintPath, codes, messages, renderable, renderNote, resolutionAvailable, requiresReview, geometrySha256, tintSha256,
  //  geometryBytes, tintBytes, looseMods, overlay: [{dataPath, file}]}]}
  Q_PROPERTY(QVariantMap matrix READ matrix NOTIFY matrixChanged)
  // {state: "empty" | "fresh" | "stale-profile" | "stale-inputs" | "unknown-candidate", a, b, chosen, savedAt, notes, path, error}
  Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged)
  // The two sources on view (candidate ids); saved only by saveDraft().
  Q_PROPERTY(QString candidateA READ candidate_a NOTIFY selectionChanged)
  Q_PROPERTY(QString candidateB READ candidate_b NOTIFY selectionChanged)
  // Changes whenever any image state changes (bindings re-read imageOf()).
  Q_PROPERTY(int imageRevision READ image_revision NOTIFY imagesChanged)
  // The A/B dialog is open. Pictures are drawn only while it is (selecting an NPC alone never renders a variant).
  Q_PROPERTY(bool compareOpen READ compare_open WRITE set_compare_open NOTIFY compareOpenChanged)
  // The Face Source Studio gallery is open: every drawable source of the NPC on view is drawn (cache first).
  Q_PROPERTY(bool galleryOpen READ gallery_open WRITE set_gallery_open NOTIFY galleryOpenChanged)
  // Face Source Index of the whole profile (sns::appearance::build_face_index over the VFS and the BSA archives):
  // {state: "none" | "scanning" | "ready" | "error", npcs, withAlternatives, looseFiles, archivedFiles, archives,
  //  scanMs, sources: [...], issues: [...], error}
  Q_PROPERTY(QVariantMap faceIndex READ face_index NOTIFY faceIndexChanged)

 public:
  explicit AppearanceSources(PortraitRenderer* portraits, QObject* parent = nullptr);
  ~AppearanceSources() override;

  [[nodiscard]] QVariantMap matrix() const { return matrix_map_; }
  [[nodiscard]] QVariantMap draft() const { return draft_map_; }
  [[nodiscard]] QString candidate_a() const { return a_; }
  [[nodiscard]] QString candidate_b() const { return b_; }
  [[nodiscard]] int image_revision() const { return image_revision_; }
  [[nodiscard]] bool compare_open() const { return compare_open_; }
  void set_compare_open(bool open);
  [[nodiscard]] bool gallery_open() const { return gallery_open_; }
  void set_gallery_open(bool open);
  [[nodiscard]] QVariantMap face_index() const { return face_index_map_; }

  // A new MO2 profile (or none): everything known is dropped and the draft book of that profile is read.
  void set_profile(SnapshotPtr snapshot, const QString& profile_key);
  // The selected NPC's control inputs (as the control map gets them) and getNpc details. Cheap when nothing changed.
  void set_npc(const QString& form_key, const QString& name, const appearance::ControlInputs& control, const QVariantMap& details);

  // The sources on view. Unknown ids are refused (false). Both are drawn if they can be.
  Q_INVOKABLE bool setCompare(const QString& a, const QString& b);
  // Asks for the picture of one source (the effective one is the gallery portrait; the others are variants).
  Q_INVOKABLE void requestImage(const QString& id);
  // {state: "none" | "preparing" | "rendering" | "ready" | "error" | "unavailable", url, reason, createdAt, renderMs,
  //  origin: "studio-render" | "cache" | "provisional", missing: [...], resources}. `revision` only makes bindings re-read it.
  Q_INVOKABLE QVariantMap imageOf(const QString& id, int revision = 0) const;
  // Saves the current A/B (and the preferred source, or "") as the NPC's draft. Empty string, or why it was refused.
  Q_INVOKABLE QString saveDraft(const QString& chosen, const QString& notes);
  Q_INVOKABLE void clearDraft();
  // Scans the profile's FaceGen (loose and BSA) in the background; started by set_profile, again on demand.
  Q_INVOKABLE void scanFaceIndex();
  // {total, rows: [{formKey, plugin, formId, name, geometryWinner, geometryArchived, tintWinner, alternatives, flags,
  //  sources, geometryPath, tintPath, looseMods, archives}]}. `names`: FormKey -> display name (studio.npcNames).
  Q_INVOKABLE QVariantMap queryFaceIndex(const QString& text, const QString& source, bool onlyAlternatives, const QString& flag,
                                         int limit, const QVariantMap& names) const;
  // Every FormKey of the index (to ask the catalog for their names), in index order.
  Q_INVOKABLE QStringList faceIndexFormKeys() const;
  // What giving the NPC on view the face of candidate `id` takes (sns::appearance::plan_face_transfer):
  // {candidate, verdict, chosen, canCreatePatch, requirement, reasons: [...], actions: [...]}.
  Q_INVOKABLE QVariantMap transferPlan(const QString& id) const;
  // Counters of this session: gathers, variant renders, cache hits, GUI-thread time of this class.
  Q_INVOKABLE QVariantMap metrics() const;
  Q_INVOKABLE void resetMetrics();

  [[nodiscard]] const appearance::SourceMatrix& source_matrix() const { return matrix_; }

 signals:
  void matrixChanged();
  void draftChanged();
  void selectionChanged();
  void imagesChanged();
  void compareOpenChanged();
  void galleryOpenChanged();
  void faceIndexChanged();

 private:
  void request_compare_images();
  void request_gallery_images();
  struct FaceArchives;
  struct FaceScan;
  void on_face_scanned(int serial, const std::shared_ptr<FaceScan>& scan);
  struct Gathered;
  struct ImageState {
    QString state = QStringLiteral("none");
    QString url, reason, created_at, origin;
    qint64 render_ms = 0;
    QStringList missing;
  };
  void start_gather();
  void on_gathered(int serial, const std::shared_ptr<Gathered>& result);
  void apply_matrix(appearance::SourceMatrix matrix, const QString& input_digest);
  void refresh_draft();
  void choose_default_selection();
  void set_image(const QString& id, ImageState state);
  void note_gui(const char* where, qint64 ms);
  bool images_emit_pending_ = false;
  qint64 last_matrix_emit_ms_ = 0, max_matrix_emit_ms_ = 0;  // QML reaction to a new matrix (see note_gui)
  [[nodiscard]] QString variant_key(const appearance::SourceCandidate& candidate) const;
  void load_book();
  bool save_book(QString* error);
  [[nodiscard]] QString variants_directory() const;

  PortraitRenderer* portraits_;
  SnapshotPtr snapshot_;
  QString profile_key_;
  // The NPC on view and what its matrix was built from.
  QString form_key_, name_;
  appearance::ControlInputs control_;
  QVariantMap details_;
  QString pre_key_;  // identity of the inputs without the files: set_npc does nothing when it is unchanged
  int serial_ = 0;   // bumps with every NPC / profile change: late results are dropped
  std::shared_ptr<std::atomic<bool>> cancel_;
  bool gathering_ = false;
  appearance::SourceMatrix matrix_;
  QString input_digest_;
  QVariantMap matrix_map_;
  qint64 gather_ms_ = 0, read_bytes_ = 0;
  QStringList skipped_;
  QString a_, b_;
  bool compare_open_ = false;
  bool gallery_open_ = false;
  // Face Source Index of the profile and the archived FaceGen copies it found (shared with gather threads).
  int face_serial_ = 0;
  std::shared_ptr<std::atomic<bool>> face_cancel_;
  appearance::FaceIndex face_index_;
  std::shared_ptr<const FaceArchives> face_archives_;
  QVariantMap face_index_map_;
  QHash<QString, ImageState> images_;  // by candidate id (the effective one is read from the gallery)
  int image_revision_ = 0;
  // The user's draft, per profile, in a file of its own. A book that exists but cannot be read is never overwritten.
  appearance::SourceDraftBook book_;
  QString book_path_;
  bool book_unreadable_ = false;
  QVariantMap draft_map_;
  // Measurements of this session.
  int gathers_ = 0, variants_requested_ = 0, variant_cache_hits_ = 0, variants_rendered_ = 0, variant_errors_ = 0;
  qint64 max_gui_ms_ = 0, total_variant_render_ms_ = 0, max_variant_render_ms_ = 0, max_gather_ms_ = 0;
  QList<qint64> gui_samples_;
};

}  // namespace sns::app
