#include "portrait_cache.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace sns::app {
namespace {

QString sha256_hex(const QByteArray& bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool write_atomic(const QString& path, const QByteArray& bytes, QString* error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    if (error) *error = file.errorString();
    return false;
  }
  return true;
}

}  // namespace

PortraitCache::PortraitCache(QString directory) : directory_(std::move(directory)) {}

QString PortraitCache::entry_name(const QString& form_key) {
  const auto colon = form_key.indexOf(':');
  bool ok = false;
  const auto id = form_key.left(colon).toUInt(&ok, 16);
  QString name = colon > 0 && ok ? QStringLiteral("%1_%2").arg(id, 8, 16, QLatin1Char('0')).arg(form_key.mid(colon + 1).toLower())
                                 : form_key.toLower();
  name.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]")), QStringLiteral("_"));
  return name;
}

QByteArray portrait_entry_to_json(const PortraitEntry& e) {
  QJsonArray resources;
  for (const auto& r : e.resources) {
    resources.append(QJsonObject{{"path", r.path}, {"location", r.location}, {"size", r.size}, {"modifiedMs", r.modified_ms}});
  }
  return QJsonDocument(QJsonObject{{"schema", PortraitEntry::kSchema},
                                   {"formKey", e.form_key},
                                   {"rendererVersion", e.renderer_version},
                                   {"snapshotDigest", e.snapshot_digest},
                                   {"resources", resources},
                                   {"imageFile", e.image_file},
                                   {"imageSha256", e.image_sha256},
                                   {"width", e.width},
                                   {"height", e.height},
                                   {"renderMs", e.render_ms},
                                   {"createdAt", e.created_at},
                                   {"source", e.source}})
      .toJson(QJsonDocument::Indented);
}

std::optional<PortraitEntry> portrait_entry_from_json(const QByteArray& json, QString* error) {
  QJsonParseError parse{};
  const auto doc = QJsonDocument::fromJson(json, &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
    if (error) *error = QStringLiteral("not a JSON object: %1").arg(parse.errorString());
    return std::nullopt;
  }
  const auto o = doc.object();
  if (o.value("schema").toInt() != PortraitEntry::kSchema) {
    if (error) *error = QStringLiteral("unknown schema %1").arg(o.value("schema").toInt());
    return std::nullopt;
  }
  PortraitEntry e;
  e.form_key = o.value("formKey").toString();
  e.renderer_version = o.value("rendererVersion").toString();
  e.snapshot_digest = o.value("snapshotDigest").toString();
  e.image_file = o.value("imageFile").toString();
  e.image_sha256 = o.value("imageSha256").toString();
  e.width = o.value("width").toInt();
  e.height = o.value("height").toInt();
  e.render_ms = o.value("renderMs").toInteger();
  e.created_at = o.value("createdAt").toString();
  e.source = o.value("source").toString();
  for (const auto& v : o.value("resources").toArray()) {
    const auto r = v.toObject();
    e.resources.push_back({r.value("path").toString(), r.value("location").toString(), r.value("size").toInteger(-1),
                           r.value("modifiedMs").toInteger(-1)});
  }
  if (e.form_key.isEmpty() || e.image_file.isEmpty() || e.image_file.contains('/') || e.image_file.contains('\\')) {
    if (error) *error = QStringLiteral("entry without FormKey or with an invalid image name");
    return std::nullopt;
  }
  return e;
}

PortraitLookup PortraitCache::lookup(const QString& form_key, const QString& renderer_version, const QString& snapshot_digest,
                                     const std::function<PortraitResource(const QString& path)>& current) const {
  PortraitLookup out;
  const QDir dir(directory_);
  QFile file(dir.filePath(entry_name(form_key) + ".json"));
  if (!file.exists()) return out;
  out.state = PortraitCacheState::Stale;
  if (!file.open(QIODevice::ReadOnly)) {
    out.reason = QStringLiteral("entry unreadable: %1").arg(file.errorString());
    return out;
  }
  QString error;
  out.entry = portrait_entry_from_json(file.readAll(), &error);
  if (!out.entry) {
    out.reason = QStringLiteral("entry invalid: %1").arg(error);
    return out;
  }
  const auto& e = *out.entry;
  if (entry_name(e.form_key) != entry_name(form_key)) {
    out.reason = QStringLiteral("entry of another NPC (%1)").arg(e.form_key);
  } else if (e.renderer_version != renderer_version) {
    out.reason = QStringLiteral("renderer %1, now %2").arg(e.renderer_version, renderer_version);
  } else if (e.snapshot_digest != snapshot_digest) {
    out.reason = QStringLiteral("MO2 profile or load order changed");
  } else if (e.source != QLatin1String("studio-render")) {
    out.reason = QStringLiteral("unknown source %1").arg(e.source);
  } else {
    QFile image(dir.filePath(e.image_file));
    if (!image.open(QIODevice::ReadOnly)) {
      out.reason = QStringLiteral("image missing: %1").arg(e.image_file);
    } else if (sha256_hex(image.readAll()) != e.image_sha256) {
      out.reason = QStringLiteral("image does not match its hash: %1").arg(e.image_file);
    } else {
      for (const auto& r : e.resources) {
        if (current(r.path) != r) {
          out.reason = QStringLiteral("resource changed: %1").arg(r.path);
          return out;
        }
      }
      out.state = PortraitCacheState::Valid;
      out.image_path = QFileInfo(image).absoluteFilePath();
    }
  }
  return out;
}

bool PortraitCache::store(PortraitEntry entry, const QByteArray& png, QString* error) {
  if (!QDir().mkpath(directory_)) {
    if (error) *error = QStringLiteral("cannot create %1").arg(directory_);
    return false;
  }
  const QDir dir(directory_);
  const auto name = entry_name(entry.form_key);
  const auto replaced = current(entry.form_key);  // only an intact render can be compared later
  const auto older = previous(entry.form_key);
  entry.source = kStudioRender;
  entry.image_sha256 = sha256_hex(png);
  entry.image_file = name + "-" + entry.image_sha256.left(12) + ".png";
  if (!write_atomic(dir.filePath(entry.image_file), png, error)) return false;
  // The render being replaced becomes the previous version (its entry keeps what it was made from) when it
  // differs: another image, or the same pixels made from other resources, profile or renderer (evidence that
  // the change did not show). The one before it goes. The cache never keeps more than these two per NPC.
  QStringList keep{entry.image_file};
  const bool differs = replaced && (replaced->entry.image_file != entry.image_file || replaced->entry.snapshot_digest != entry.snapshot_digest ||
                                    replaced->entry.renderer_version != entry.renderer_version ||
                                    !changed_resources(replaced->entry, entry).isEmpty());
  if (differs) {
    if (!write_atomic(dir.filePath(name + ".previous.json"), portrait_entry_to_json(replaced->entry), error)) return false;
    keep << replaced->entry.image_file;
  } else if (older) {
    keep << older->entry.image_file;
  }
  if (!write_atomic(dir.filePath(name + ".json"), portrait_entry_to_json(entry), error)) return false;
  for (const auto& gone : {replaced ? replaced->entry.image_file : QString(), older ? older->entry.image_file : QString()}) {
    if (!gone.isEmpty() && !keep.contains(gone)) QFile::remove(dir.filePath(gone));
  }
  return true;
}

std::optional<PortraitVersion> PortraitCache::read_version(const QString& json_name, const QString& form_key, QString* reason) const {
  const auto fail = [reason](const QString& why) {
    if (reason) *reason = why;
    return std::optional<PortraitVersion>{};
  };
  const QDir dir(directory_);
  QFile file(dir.filePath(json_name));
  if (!file.exists()) return fail(QStringLiteral("none"));
  if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("entry unreadable: %1").arg(file.errorString()));
  QString error;
  auto entry = portrait_entry_from_json(file.readAll(), &error);
  if (!entry) return fail(QStringLiteral("entry invalid: %1").arg(error));
  if (entry_name(entry->form_key) != entry_name(form_key)) return fail(QStringLiteral("entry of another NPC (%1)").arg(entry->form_key));
  QFile image(dir.filePath(entry->image_file));
  if (!image.open(QIODevice::ReadOnly)) return fail(QStringLiteral("image missing: %1").arg(entry->image_file));
  if (sha256_hex(image.readAll()) != entry->image_sha256) return fail(QStringLiteral("image does not match its hash: %1").arg(entry->image_file));
  return PortraitVersion{std::move(*entry), QFileInfo(image).absoluteFilePath()};
}

std::optional<PortraitVersion> PortraitCache::current(const QString& form_key, QString* reason) const {
  auto v = read_version(entry_name(form_key) + ".json", form_key, reason);
  if (v && v->entry.source != kStudioRender) {
    if (reason) *reason = QStringLiteral("unknown source %1").arg(v->entry.source);
    return std::nullopt;
  }
  return v;
}

std::optional<PortraitVersion> PortraitCache::previous(const QString& form_key, QString* reason) const {
  auto v = read_version(entry_name(form_key) + ".previous.json", form_key, reason);
  if (v && v->entry.source != kStudioRender) {
    if (reason) *reason = QStringLiteral("unknown source %1").arg(v->entry.source);
    return std::nullopt;
  }
  return v;
}

bool PortraitCache::store_game_capture(const QString& form_key, const QString& image_file, const QString& imported_at, QString* error) {
  const auto fail = [error](const QString& why) {
    if (error) *error = why;
    return false;
  };
  const QFileInfo info(image_file);
  if (!info.isFile()) return fail(QStringLiteral("not a file: %1").arg(image_file));
  if (info.size() > kMaxCaptureBytes) return fail(QStringLiteral("larger than %1 MB").arg(kMaxCaptureBytes / (1024 * 1024)));
  QFile in(image_file);
  if (!in.open(QIODevice::ReadOnly)) return fail(in.errorString());
  const auto bytes = in.readAll();
  QBuffer buffer;
  buffer.setData(bytes);
  QImageReader reader(&buffer);
  const auto format = QString::fromLatin1(reader.format()).toLower();
  const auto size = reader.size();
  if (format.isEmpty() || !size.isValid() || size.isEmpty()) return fail(QStringLiteral("not a readable image: %1").arg(reader.errorString()));
  if (!QDir().mkpath(directory_)) return fail(QStringLiteral("cannot create %1").arg(directory_));
  const QDir dir(directory_);
  const auto name = entry_name(form_key);
  const auto old = game_capture(form_key);
  PortraitEntry entry;
  entry.form_key = form_key;
  entry.source = kSkyrimCapture;
  // Where it came from, as the file was when it was attached: the studio cannot tell how it was taken.
  entry.resources.push_back({info.fileName(), QStringLiteral("user-file:") + info.absoluteFilePath(), info.size(), info.lastModified().toMSecsSinceEpoch()});
  entry.width = size.width();
  entry.height = size.height();
  entry.created_at = imported_at;
  entry.image_sha256 = sha256_hex(bytes);
  entry.image_file = name + "-skyrim-" + entry.image_sha256.left(12) + "." + (format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format);
  QString write_error;
  if (!write_atomic(dir.filePath(entry.image_file), bytes, &write_error) ||
      !write_atomic(dir.filePath(name + ".skyrim.json"), portrait_entry_to_json(entry), &write_error)) {
    return fail(write_error);
  }
  if (old && old->entry.image_file != entry.image_file) QFile::remove(old->image_path);
  return true;
}

std::optional<PortraitVersion> PortraitCache::game_capture(const QString& form_key, QString* reason) const {
  auto v = read_version(entry_name(form_key) + ".skyrim.json", form_key, reason);
  if (v && v->entry.source != kSkyrimCapture) {
    if (reason) *reason = QStringLiteral("unknown source %1").arg(v->entry.source);
    return std::nullopt;
  }
  return v;
}

bool PortraitCache::remove_game_capture(const QString& form_key) {
  const auto v = game_capture(form_key);
  const bool removed = QFile::remove(QDir(directory_).filePath(entry_name(form_key) + ".skyrim.json"));
  if (v) QFile::remove(v->image_path);
  return removed;
}

QStringList PortraitCache::changed_resources(const PortraitEntry& before, const PortraitEntry& after) {
  QHash<QString, PortraitResource> a, b;
  for (const auto& r : before.resources) a.insert(r.path.toLower(), r);
  for (const auto& r : after.resources) b.insert(r.path.toLower(), r);
  QStringList out;
  for (auto it = a.cbegin(); it != a.cend(); ++it) {
    const auto other = b.constFind(it.key());
    if (other == b.cend() || !(other.value() == it.value())) out << it.value().path;
  }
  for (auto it = b.cbegin(); it != b.cend(); ++it) {
    if (!a.contains(it.key())) out << it.value().path;
  }
  out.sort(Qt::CaseInsensitive);
  return out;
}

QHash<QString, PortraitVersion> PortraitCache::scan_current() const {
  QHash<QString, PortraitVersion> out;
  const QDir dir(directory_);
  for (const auto& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
    if (name.endsWith(QLatin1String(".previous.json")) || name.endsWith(QLatin1String(".skyrim.json"))) continue;
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::ReadOnly)) continue;
    const auto entry = portrait_entry_from_json(file.readAll(), nullptr);
    if (!entry || entry->source != kStudioRender || entry_name(entry->form_key) + ".json" != name) continue;
    if (auto v = read_version(name, entry->form_key, nullptr)) out.insert(entry_name(entry->form_key), std::move(*v));
  }
  return out;
}

PortraitCache::Usage PortraitCache::usage() const {
  Usage u;
  const QDir dir(directory_);
  for (const auto& info : dir.entryInfoList(QDir::Files)) {
    ++u.files;
    u.bytes += info.size();
    const auto name = info.fileName();
    if (name.endsWith(QLatin1String(".skyrim.json"))) ++u.captures;
    else if (name.endsWith(QLatin1String(".json")) && !name.endsWith(QLatin1String(".previous.json"))) ++u.renders;
  }
  return u;
}

int PortraitCache::clear_renders(QString* error) {
  const QDir dir(directory_);
  if (!dir.exists()) return 0;
  int removed = 0;
  QStringList failed;
  const auto remove = [&](const QString& name) {
    if (QFile::remove(dir.filePath(name))) ++removed;
    else if (QFile::exists(dir.filePath(name))) failed << name;
  };
  for (const auto& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
    if (!name.endsWith(QLatin1String(".skyrim.json"))) remove(name);
  }
  // Studio renders are "<name>-<hash>.png"; a capture image always has "-skyrim-" in its name.
  for (const auto& name : dir.entryList({QStringLiteral("*.png")}, QDir::Files)) {
    if (!name.contains(QLatin1String("-skyrim-"))) remove(name);
  }
  if (!failed.isEmpty()) {
    if (error) *error = QStringLiteral("could not remove: %1").arg(failed.join(QStringLiteral(", ")));
    return -1;
  }
  return removed;
}

QList<PortraitCache::InspectRow> PortraitCache::inspect(const QString& renderer_version, const QString& snapshot_digest,
                                                        const std::function<PortraitResource(const QString& path)>& current) const {
  QList<InspectRow> rows;
  const QDir dir(directory_);
  QSet<QString> referenced;  // lower-case image file names some entry names
  const auto size_of = [&](const QString& file) {
    const QFileInfo info(dir.filePath(file));
    return info.isFile() ? info.size() : qint64{0};
  };
  for (const auto& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
    InspectRow row;
    const bool capture = name.endsWith(QLatin1String(".skyrim.json"));
    const bool previous = name.endsWith(QLatin1String(".previous.json"));
    row.kind = capture ? QStringLiteral("capture") : previous ? QStringLiteral("previous") : QStringLiteral("render");
    row.name = name.left(name.size() - (capture ? 12 : previous ? 14 : 5));
    row.files = {name};
    row.bytes = size_of(name);
    const auto damaged = [&row](const QString& code, const QString& detail = {}) {
      row.state = QStringLiteral("damaged");
      row.code = code;
      row.detail = detail;
    };
    QFile file(dir.filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
      damaged(QStringLiteral("entry-unreadable"), file.errorString());
      rows.push_back(std::move(row));
      continue;
    }
    QString error;
    const auto entry = portrait_entry_from_json(file.readAll(), &error);
    if (!entry) {
      damaged(QStringLiteral("entry-invalid"), error);
      rows.push_back(std::move(row));
      continue;
    }
    const auto& e = *entry;
    row.form_key = e.form_key;
    row.created_at = e.created_at;
    row.renderer_version = e.renderer_version;
    row.files << e.image_file;
    referenced.insert(e.image_file.toLower());
    QFile image(dir.filePath(e.image_file));
    const bool image_there = image.open(QIODevice::ReadOnly);
    row.bytes += image_there ? image.size() : 0;
    const auto want_source = capture ? kSkyrimCapture : kStudioRender;
    if (entry_name(e.form_key) != row.name) {
      damaged(QStringLiteral("other-npc"), e.form_key);
    } else if (e.source != want_source) {
      damaged(QStringLiteral("unknown-source"), e.source);
    } else if (row.kind == QLatin1String("render") && e.renderer_version != renderer_version) {
      row.state = QStringLiteral("stale");
      row.code = QStringLiteral("renderer-changed");
      row.detail = QStringLiteral("%1 → %2").arg(e.renderer_version, renderer_version);
    } else if (row.kind == QLatin1String("render") && e.snapshot_digest != snapshot_digest) {
      row.state = QStringLiteral("stale");
      row.code = QStringLiteral("profile-changed");
    } else if (!image_there) {
      damaged(QStringLiteral("image-missing"), e.image_file);
    } else if (sha256_hex(image.readAll()) != e.image_sha256) {
      damaged(QStringLiteral("image-hash"), e.image_file);
    } else if (row.kind != QLatin1String("render")) {
      row.state = QStringLiteral("kept");
      row.code = QStringLiteral("ok");
    } else {
      QStringList changed;
      for (const auto& r : e.resources) {
        if (current(r.path) != r) changed << r.path;
      }
      row.changed_resources = static_cast<int>(changed.size());
      if (changed.isEmpty()) {
        row.state = QStringLiteral("valid");
        row.code = QStringLiteral("ok");
      } else {
        changed.sort(Qt::CaseInsensitive);
        row.state = QStringLiteral("stale");
        row.code = QStringLiteral("resource-changed");
        row.detail = changed.first();
      }
    }
    rows.push_back(std::move(row));
  }
  // Images no entry names: the ones the studio made ("<name>-<hash>.png") can go; anything else is only listed.
  for (const auto& info : dir.entryInfoList(QDir::Files)) {
    const auto name = info.fileName();
    if (name.endsWith(QLatin1String(".json")) || referenced.contains(name.toLower())) continue;
    InspectRow row;
    row.kind = QStringLiteral("orphan");
    row.state = QStringLiteral("orphan");
    const bool studio_png = name.endsWith(QLatin1String(".png"), Qt::CaseInsensitive) && !name.contains(QLatin1String("-skyrim-"));
    row.code = studio_png ? QStringLiteral("orphan-image") : QStringLiteral("unknown-file");
    row.detail = name;
    row.files = {name};
    row.bytes = info.size();
    rows.push_back(std::move(row));
  }
  // Worst first, so the list opens on what needs a decision.
  const auto rank = [](const InspectRow& r) {
    if (r.kind == QLatin1String("render") && r.state == QLatin1String("stale")) return 0;
    if (r.state == QLatin1String("damaged")) return 1;
    if (r.state == QLatin1String("orphan")) return 2;
    if (r.state == QLatin1String("valid")) return 3;
    return 4;
  };
  std::stable_sort(rows.begin(), rows.end(), [&](const InspectRow& a, const InspectRow& b) {
    return rank(a) != rank(b) ? rank(a) < rank(b) : a.name < b.name;
  });
  return rows;
}

bool PortraitCache::is_unusable(const InspectRow& row) {
  if (row.kind == QLatin1String("render")) return row.state == QLatin1String("stale") || row.state == QLatin1String("damaged");
  if (row.kind == QLatin1String("previous")) return row.state == QLatin1String("damaged");
  if (row.kind == QLatin1String("orphan")) return row.code == QLatin1String("orphan-image");
  return false;  // captures are the user's; kept and valid rows are in use
}

PortraitCache::PurgeResult PortraitCache::remove_unusable(const QString& renderer_version, const QString& snapshot_digest,
                                                          const std::function<PortraitResource(const QString& path)>& current, QString* error) {
  PurgeResult result;
  const QDir dir(directory_);
  const auto rows = inspect(renderer_version, snapshot_digest, current);
  QSet<QString> keep;  // files a row that stays still names (a render and the one it replaced can share one image)
  for (const auto& row : rows) {
    if (is_unusable(row)) continue;
    for (const auto& f : row.files) keep.insert(f.toLower());
  }
  QStringList failed;
  for (const auto& row : rows) {
    if (!is_unusable(row)) continue;
    ++result.rows;
    if (row.kind == QLatin1String("render") && !row.form_key.isEmpty()) result.form_keys << row.form_key;
    for (const auto& f : row.files) {
      if (keep.contains(f.toLower())) continue;
      const QFileInfo info(dir.filePath(f));
      if (!info.isFile()) continue;
      const auto size = info.size();
      if (QFile::remove(info.absoluteFilePath())) {
        ++result.files;
        result.bytes += size;
      } else {
        failed << f;
      }
    }
  }
  if (!failed.isEmpty() && error) *error = QStringLiteral("could not remove: %1").arg(failed.join(QStringLiteral(", ")));
  return result;
}

}  // namespace sns::app
