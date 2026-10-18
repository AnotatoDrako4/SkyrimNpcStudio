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

}  // namespace sns::app
