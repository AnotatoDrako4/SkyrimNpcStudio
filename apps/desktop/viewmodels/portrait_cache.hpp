#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QString>
#include <functional>
#include <optional>

namespace sns::app {

// One resource a portrait was rendered from, as the profile had it then: a loose file by its absolute
// path, size and modification time; a file inside an archive by the archive and its MO2 provider.
struct PortraitResource {
  QString path;      // data path (meshes\..., textures\...)
  QString location;  // "loose:<absolute path>", "archive:<archive>@<provider>" or "missing"
  qint64 size = -1;
  qint64 modified_ms = -1;
  bool operator==(const PortraitResource&) const = default;
};

// The cache entry of one NPC portrait. `source` is always "studio-render": a render of the studio from
// the profile's resources, never a capture made inside Skyrim.
struct PortraitEntry {
  static constexpr int kSchema = 1;
  QString form_key;
  QString renderer_version;
  QString snapshot_digest;
  QList<PortraitResource> resources;
  QString image_file;  // file name inside the cache folder
  QString image_sha256;
  int width = 0;
  int height = 0;
  qint64 render_ms = 0;
  QString created_at;  // ISO 8601 with its UTC offset
  QString source = QStringLiteral("studio-render");
};

enum class PortraitCacheState { Missing, Valid, Stale };

struct PortraitLookup {
  PortraitCacheState state = PortraitCacheState::Missing;
  std::optional<PortraitEntry> entry;  // when an entry was read
  QString reason;                      // why it is stale
  QString image_path;                  // absolute, when valid
};

// One stored image with its entry, read back from disk: the image bytes match the entry's hash.
struct PortraitVersion {
  PortraitEntry entry;
  QString image_path;  // absolute
};

// Portrait images on disk, one PNG and one JSON entry per NPC, in a folder of their own (per profile).
// An entry is used only while everything it was rendered from is unchanged. Next to it the cache may keep
// the render it replaced (`<name>.previous.json`, for comparison) and a capture made inside Skyrim that the
// user attached (`<name>.skyrim.json`, source "skyrim-capture"); neither is ever shown as the card portrait.
class PortraitCache {
 public:
  static inline const QString kStudioRender = QStringLiteral("studio-render");
  static inline const QString kSkyrimCapture = QStringLiteral("skyrim-capture");
  static constexpr qint64 kMaxCaptureBytes = 32ll * 1024 * 1024;

  explicit PortraitCache(QString directory);
  [[nodiscard]] const QString& directory() const { return directory_; }
  // File stem of an NPC: the same for "0A2C8E:Skyrim.esm" and "000A2C8E:skyrim.esm".
  [[nodiscard]] static QString entry_name(const QString& form_key);
  // Valid only when the renderer version and the snapshot digest match, the image is there with its
  // hash, and every resource is still as recorded (`current` reads one resource from the profile now).
  [[nodiscard]] PortraitLookup lookup(const QString& form_key, const QString& renderer_version, const QString& snapshot_digest,
                                      const std::function<PortraitResource(const QString& path)>& current) const;
  // Writes the PNG and then its entry, each atomically. The render it replaces (a different image of this
  // NPC) becomes the previous version; the one before that is removed. Fills image_file and image_sha256.
  bool store(PortraitEntry entry, const QByteArray& png, QString* error);
  // The render that the current one replaced, when its image is still there with its hash.
  [[nodiscard]] std::optional<PortraitVersion> previous(const QString& form_key, QString* reason = nullptr) const;
  // The current entry as stored (hash checked, not checked against the profile).
  [[nodiscard]] std::optional<PortraitVersion> current(const QString& form_key, QString* reason = nullptr) const;
  // Copies an image file the user chose (a screenshot taken in Skyrim) as this NPC's game capture. The file
  // must be a readable image no larger than kMaxCaptureBytes; its origin is recorded, nothing is inferred.
  bool store_game_capture(const QString& form_key, const QString& image_file, const QString& imported_at, QString* error);
  [[nodiscard]] std::optional<PortraitVersion> game_capture(const QString& form_key, QString* reason = nullptr) const;
  bool remove_game_capture(const QString& form_key);
  // Data paths whose record differs between two renders (also those only one of them used).
  [[nodiscard]] static QStringList changed_resources(const PortraitEntry& before, const PortraitEntry& after);

  // Every current studio render of the folder read back with its hash, keyed by entry_name. Nothing is
  // checked against the profile: the gallery shows these at once as *provisional* ("cached, being verified")
  // while lookup() decides whether each one is still valid. Runs off the GUI thread (it reads every image).
  [[nodiscard]] QHash<QString, PortraitVersion> scan_current() const;
  // What the folder holds: studio renders (current entries), Skyrim captures, files and bytes.
  struct Usage {
    int renders = 0;
    int captures = 0;
    int files = 0;
    qint64 bytes = 0;
  };
  [[nodiscard]] Usage usage() const;
  // Removes the studio renders only: current and previous entries and their PNGs. The captures the user
  // attached (`.skyrim.json` and their images) stay. Nothing outside this folder is touched. Returns the
  // number of files removed (-1 and `error` when one could not be removed).
  int clear_renders(QString* error);

 private:
  [[nodiscard]] std::optional<PortraitVersion> read_version(const QString& json_name, const QString& form_key, QString* reason) const;
  QString directory_;
};

[[nodiscard]] QByteArray portrait_entry_to_json(const PortraitEntry& entry);
[[nodiscard]] std::optional<PortraitEntry> portrait_entry_from_json(const QByteArray& json, QString* error);

}  // namespace sns::app
