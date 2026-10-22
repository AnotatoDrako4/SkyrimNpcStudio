#include "preset_head_checker.hpp"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

#include "diagnostics/crash_diagnostics.hpp"
#include "json_bridge.hpp"
#include "resource_context.hpp"
#include "sns/assets/asset_resolver.hpp"
#include "sns/core/text.hpp"
#include "sns/ipc/protocol.hpp"
#include "sns/mo2/confined_file.hpp"
#include "sns/nif/mesh.hpp"
#include "sns/presets/jslot_parser.hpp"

namespace sns::app {
namespace fs = std::filesystem;
namespace method = ipc::protocol::method;
using namespace std::chrono_literals;

namespace {

constexpr qint64 kMaxHeadBytes = 48ll * 1024 * 1024;
constexpr qint64 kMaxJslotBytes = 8ll * 1024 * 1024;
constexpr std::size_t kMaxHeads = 400;
constexpr int kMaxMissingPerShape = 12;

bool read_bytes(const fs::path& file, qint64 cap, std::string* out) {
  std::error_code ec;
  const auto size = static_cast<qint64>(fs::file_size(file, ec));
  if (ec || size > cap) return false;
  std::ifstream in(file, std::ios::binary);
  if (!in) return false;
  out->resize(static_cast<std::size_t>(size));
  in.read(out->data(), size);
  return in.gcount() == size;
}

QString iso_of(qint64 secs) { return secs > 0 ? QDateTime::fromSecsSinceEpoch(secs).toString(Qt::ISODate) : QString(); }

QStringList list_of(const std::vector<std::string>& items) {
  QStringList out;
  for (const auto& i : items) out << qs(i);
  return out;
}

}  // namespace

struct PresetHeadChecker::Head {
  QString path;  // "<mod>: SKSE\Plugins\CharGen\x.nif"
  QString mod;
  QString stem;
  bool readable = false;
  qint64 modified = 0;
  std::vector<presets::HeadShape> shapes;
  std::vector<QStringList> missing;  // per shape: textures it names that the profile does not have
};

struct PresetHeadChecker::Index {
  std::vector<Head> heads;
  qint64 ms = 0, bytes = 0;
  int unreadable = 0;
};

struct PresetHeadChecker::Parts {
  QString path, name;
  presets::PresetHeadFacts facts;
  QString face_model;  // Data path of the Face part's base mesh
  bool vertices_done = false;
  QVariantList parts;  // resolved head parts, for the report
  QStringList unresolved;
};

PresetHeadChecker::PresetHeadChecker(WorkerClient* worker, QObject* parent) : QObject(parent), worker_(worker) {
  heads_ = {{"state", "none"}};
  report_ = {{"state", "none"}};
}

PresetHeadChecker::~PresetHeadChecker() = default;

void PresetHeadChecker::set_profile(SnapshotPtr snapshot, const QString& profile_key) {
  ++profile_serial_;
  ++check_serial_;
  snapshot_ = std::move(snapshot);
  index_.reset();
  parts_.reset();
  indexing_ = false;
  manual_.clear();
  chosen_at_.clear();
  current_path_.clear();
  profile_key_ = profile_key;
  store_path_.clear();
  store_unreadable_ = false;
  persist_error_.clear();
  if (snapshot_ && !profile_key.isEmpty()) {
    const auto dir = qEnvironmentVariableIsSet("SNS_PRESET_HEAD_DIR")
                         ? qEnvironmentVariable("SNS_PRESET_HEAD_DIR")
                         : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/preset-head-choices";
    auto file_name = profile_key;
    file_name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    store_path_ = QDir(dir).filePath(file_name + ".json");
    load_choices();
  }
  heads_ = {{"state", "none"}};
  report_ = {{"state", "none"}};
  emit headsChanged();
  emit reportChanged();
}

void PresetHeadChecker::load_choices() {
  QFile file(store_path_);
  if (!file.exists()) return;
  if (!file.open(QIODevice::ReadOnly)) {
    store_unreadable_ = true;
    persist_error_ = tr("No se puede leer el archivo de elecciones: %1").arg(file.errorString());
    return;
  }
  QJsonParseError parse{};
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject() || doc.object().value("schema").toInt() != 1) {
    // Never treat an unreadable file as empty and overwrite it: the person may still want what it holds.
    store_unreadable_ = true;
    persist_error_ = tr("El archivo de elecciones no es válido (%1): no se sobrescribe; las elecciones de esta sesión no se guardarán.")
                         .arg(parse.error != QJsonParseError::NoError ? parse.errorString() : tr("esquema desconocido"));
    return;
  }
  for (const auto& v : doc.object().value("choices").toArray()) {
    const auto o = v.toObject();
    const auto preset = o.value("preset").toString();
    const auto head = o.value("head").toString();
    if (preset.isEmpty() || head.isEmpty()) continue;
    manual_.insert(preset.toLower(), head);
    chosen_at_.insert(preset.toLower(), o.value("chosenAt").toString());
  }
}

QString PresetHeadChecker::save_choices() {
  if (store_path_.isEmpty()) return {};  // no profile key: the choices last the session
  if (store_unreadable_) return persist_error_;
  QJsonArray choices;
  for (auto it = manual_.cbegin(); it != manual_.cend(); ++it) {
    choices.append(QJsonObject{{"preset", it.key()}, {"head", it.value()}, {"chosenAt", chosen_at_.value(it.key())}});
  }
  if (!QDir().mkpath(QFileInfo(store_path_).absolutePath())) return tr("No se puede crear la carpeta de elecciones");
  QSaveFile file(store_path_);
  const auto bytes = QJsonDocument(QJsonObject{{"schema", 1}, {"profileKey", profile_key_}, {"choices", choices}}).toJson(QJsonDocument::Indented);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return tr("No se pudo guardar la elección: %1").arg(file.errorString());
  return {};
}

void PresetHeadChecker::reindex() {
  if (!snapshot_) return;
  index_.reset();
  start_index();
}

// ------------------------------------------------------------------ index of the Export Heads of the profile

void PresetHeadChecker::start_index() {
  if (!snapshot_ || indexing_) return;
  indexing_ = true;
  heads_ = {{"state", "indexing"}};
  emit headsChanged();
  const int serial = profile_serial_;
  QThreadPool::globalInstance()->start([self = QPointer<PresetHeadChecker>(this), snapshot = snapshot_, serial] {
    QElapsedTimer clock;
    clock.start();
    auto index = std::make_shared<Index>();
    const auto ctx = ResourceContext::for_snapshot(snapshot);
    std::map<std::string, Head> by_key;  // lower-case relative path -> head: a higher MO2 priority replaces a lower one
    for (const auto& provider : snapshot->vfs.providers()) {
      std::error_code ec;
      const fs::path dir = provider.root / "SKSE" / "Plugins" / "CharGen";
      if (!fs::is_directory(dir, ec)) continue;
      for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec) || !text::iequals_ascii(text::path_to_utf8(it->path().extension()), ".nif")) continue;
        const auto relative = text::path_to_utf8(fs::relative(it->path(), provider.root, file_ec));
        if (file_ec || relative.empty()) continue;
        const auto key = text::to_lower_ascii(relative);
        if (by_key.size() >= kMaxHeads && !by_key.count(key)) continue;
        // Confined to the mod's folder like every read of another mod's file (no links or junctions that leave it).
        const auto found = mo2::confined_file(provider.root, relative);
        if (found.result != mo2::ConfineResult::Found) continue;
        Head h;
        h.mod = qs(provider.name);
        h.path = h.mod + QStringLiteral(": ") + QString::fromStdString(relative);
        h.stem = qs(presets::normalize_head_stem(relative));
        h.modified = QFileInfo(QString::fromStdWString(found.path.wstring())).lastModified().toSecsSinceEpoch();
        std::string bytes;
        if (read_bytes(found.path, kMaxHeadBytes, &bytes)) {
          index->bytes += static_cast<qint64>(bytes.size());
          try {
            const auto mesh = nif::load_mesh(bytes, relative);  // the studio's safe loader; throws when the file is damaged
            h.readable = true;
            for (const auto& shape : mesh.shapes) {
              h.shapes.push_back({shape.name, static_cast<std::uint32_t>(shape.positions.size())});
              QStringList missing;
              for (const auto& tex : shape.textures) {
                if (tex.empty() || missing.size() >= kMaxMissingPerShape) continue;
                if (!ctx || !ctx->resolver || ctx->resolver->locate(tex)) continue;
                // RaceMenu writes the head's own texture next to the NIF (Data\SKSE\Plugins\CharGen\x.dds) and names it from the game
                // root, so the loader's "textures\" prefix does not apply: that location is where it really is.
                static constexpr std::string_view kChargen = "skse\\plugins\\chargen\\";
                const auto tex_key = text::to_lower_ascii(tex);
                if (const auto at = tex_key.find(kChargen); at != std::string::npos && ctx->resolver->locate(tex_key.substr(at))) continue;
                missing << qs(tex);
              }
              h.missing.push_back(missing);
            }
          } catch (const std::exception&) {
            h.readable = false;
          }
        }
        by_key[key] = std::move(h);
      }
    }
    for (auto& [key, head] : by_key) {
      index->unreadable += head.readable ? 0 : 1;
      index->heads.push_back(std::move(head));
    }
    index->ms = clock.elapsed();
    QMetaObject::invokeMethod(
        self.data(), [self, serial, index] { if (self) self->on_index(serial, index); }, Qt::QueuedConnection);
  });
}

void PresetHeadChecker::on_index(int serial, const std::shared_ptr<Index>& index) {
  if (serial != profile_serial_) return;
  indexing_ = false;
  index_ = index;
  last_index_ms_ = index->ms;
  heads_ = {{"state", "ready"},
            {"count", static_cast<int>(index->heads.size())},
            {"readable", static_cast<int>(index->heads.size()) - index->unreadable},
            {"unreadable", index->unreadable},
            {"bytes", index->bytes},
            {"ms", index->ms}};
  diag::record("presetheads", QStringLiteral("indexed %1 export heads (%2 unreadable) in %3 ms, %4 bytes")
                                  .arg(index->heads.size()).arg(index->unreadable).arg(index->ms).arg(index->bytes).toStdString());
  emit headsChanged();
  if (!current_path_.isEmpty() && report_.value("state").toString() == QLatin1String("indexing")) start_check(current_path_);
}

// ------------------------------------------------------------------ one preset

void PresetHeadChecker::set_report(QVariantMap report) {
  report_ = std::move(report);
  emit reportChanged();
}

void PresetHeadChecker::check(const QString& jslot_path) {
  ++check_serial_;
  current_path_ = jslot_path;
  parts_.reset();
  if (!snapshot_) {
    set_report({{"state", "error"}, {"error", tr("No hay un perfil cargado")}});
    return;
  }
  if (!index_) {
    set_report({{"state", "indexing"}, {"preset", QVariantMap{{"name", QFileInfo(jslot_path).completeBaseName()}, {"path", jslot_path}}}});
    start_index();
    return;
  }
  start_check(jslot_path);
}

void PresetHeadChecker::start_check(const QString& jslot_path) {
  const int serial = check_serial_;
  const auto name = QFileInfo(jslot_path).completeBaseName();
  set_report({{"state", "checking"}, {"preset", QVariantMap{{"name", name}, {"path", jslot_path}}}});
  const auto fail = [this, serial, name, jslot_path](const QString& why) {
    if (serial != check_serial_) return;
    set_report({{"state", "error"}, {"error", why}, {"preset", QVariantMap{{"name", name}, {"path", jslot_path}}}});
  };
  // The preset file: size-limited, parsed by the studio's own parser (which refuses hostile numbers and shapes).
  QFile file(jslot_path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxJslotBytes) {
    fail(tr("No se puede leer el preset (%1)").arg(file.errorString()));
    return;
  }
  const auto text = file.readAll().toStdString();
  auto parts = std::make_shared<Parts>();
  parts->path = jslot_path;
  parts->name = name;
  parts->facts.preset_name = ss(name);
  std::vector<std::string> keys;
  try {
    const auto preset = presets::parse_jslot(text, ss(name));
    for (const auto& hp : preset.head_parts) {
      if (!hp.form_key.empty() && std::find(keys.begin(), keys.end(), hp.form_key) == keys.end()) keys.push_back(hp.form_key);
    }
  } catch (const std::exception& e) {
    fail(tr("El archivo no es un preset de RaceMenu legible: %1").arg(QString::fromUtf8(e.what())));
    return;
  }
  Json formKeys = Json::array();
  for (const auto& k : keys) formKeys.push_back(k);
  worker_->call(
      method::kSearchHeadParts, Json{{"formKeys", formKeys}, {"limit", 200}}, 30s, this,
      [this, serial, parts, keys](const Json& r) {
        if (serial != check_serial_) return;
        std::map<std::string, Json> by_key;
        for (const auto& item : r.value("items", Json::array())) by_key[text::to_lower_ascii(item.value("formKey", ""))] = item;
        for (const auto& k : keys) {
          const auto it = by_key.find(text::to_lower_ascii(k));
          if (it == by_key.end()) {
            parts->unresolved << qs(k);
            parts->facts.unresolved.push_back(k);
            continue;
          }
          const auto& item = it->second;
          const auto editor_id = item.value("editorId", "");
          const auto type = item.value("type", "");
          const auto model = item.value("modelPath", "");
          parts->parts << QVariantMap{{"formKey", qs(k)}, {"editorId", qs(editor_id)}, {"type", qs(type)}, {"modelPath", qs(model)}};
          if (model.empty() || editor_id.empty()) continue;  // a part without geometry has no shape in a head
          parts->facts.shape_names.push_back(editor_id);
          if (type == "Face" && parts->facts.face_part.empty()) {
            parts->facts.face_part = editor_id;
            auto data = qs(model).replace(QLatin1Char('/'), QLatin1Char('\\'));
            parts->face_model = data.startsWith(QLatin1String("meshes\\"), Qt::CaseInsensitive) ? data : QStringLiteral("meshes\\") + data;
          }
        }
        parts_ = parts;
        run_match();
      },
      [this, serial, name, jslot_path](int, const QString& message, const Json&) {
        if (serial != check_serial_) return;
        set_report({{"state", "error"}, {"error", tr("El worker no pudo resolver las piezas de cabeza: %1").arg(message)},
                    {"preset", QVariantMap{{"name", name}, {"path", jslot_path}}}});
      });
}

void PresetHeadChecker::run_match() {
  if (!parts_ || !index_ || !snapshot_) return;
  const int serial = check_serial_;
  const auto manual_path = manual_.value(current_path_.toLower());
  const auto persisted = store_path_.isEmpty() ? QString() : QDir::toNativeSeparators(store_path_);
  const auto persist_error = persist_error_;
  QThreadPool::globalInstance()->start([self = QPointer<PresetHeadChecker>(this), snapshot = snapshot_, index = index_, parts = *parts_, manual_path, persisted, persist_error, serial] {
    QElapsedTimer clock;
    clock.start();
    auto facts = parts.facts;
    std::optional<std::uint32_t> vertices_found;
    bool vertices_attempted = parts.vertices_done;
    if (!parts.vertices_done && !parts.face_model.isEmpty()) {
      vertices_attempted = true;
      // The vertex count of the Face part's own mesh: what an Export Head's face must have to replace it.
      const auto ctx = ResourceContext::for_snapshot(snapshot);
      if (ctx && ctx->resolver) {
        if (const auto bytes = ctx->resolver->read(ss(parts.face_model))) {
          try {
            const auto mesh = nif::load_mesh(*bytes, ss(parts.face_model));
            const nif::Shape* best = nullptr;
            for (const auto& s : mesh.shapes) {
              if (text::iequals_ascii(s.name, facts.face_part)) { best = &s; break; }
              if (!best || s.positions.size() > best->positions.size()) best = &s;
            }
            if (best && !best->positions.empty()) vertices_found = static_cast<std::uint32_t>(best->positions.size());
          } catch (const std::exception&) {
          }
        }
      }
      if (vertices_found) facts.face_vertices = vertices_found;
    }
    const auto face = text::to_lower_ascii(facts.face_part);
    std::vector<presets::HeadCandidate> candidates;
    std::optional<std::size_t> manual;
    for (std::size_t i = 0; i < index->heads.size(); ++i) {
      const auto& h = index->heads[i];
      presets::HeadCandidate c;
      c.path = ss(h.path);
      c.stem = ss(h.stem);
      c.readable = h.readable;
      c.shapes = h.shapes;
      c.modified = h.modified;
      for (std::size_t s = 0; s < h.shapes.size(); ++s) {
        if (text::to_lower_ascii(h.shapes[s].name) == face && s < h.missing.size()) {
          for (const auto& t : h.missing[s]) c.missing_textures.push_back(ss(t));
        }
      }
      if (!manual_path.isEmpty() && h.path == manual_path) manual = i;
      candidates.push_back(std::move(c));
    }
    const auto match = presets::match_export_head(facts, candidates, manual);

    QVariantMap report{{"state", "ready"},
                       {"preset", QVariantMap{{"name", parts.name}, {"path", parts.path}}},
                       {"verdict", qs(std::string(presets::to_string(match.verdict)))},
                       {"codes", list_of(match.codes)},
                       {"message", qs(match.message)},
                       {"caveats", list_of(match.caveats)},
                       {"needsManualChoice", match.needs_manual_choice},
                       {"manual", manual.has_value()},
                       // A choice remembered from an earlier session whose head is no longer in the profile is ignored, and said so.
                       {"manualStale", !manual_path.isEmpty() && !manual.has_value()},
                       {"manualHead", manual_path},
                       {"choicesFile", persisted},
                       {"persistError", persist_error},
                       {"parts", parts.parts},
                       {"unresolved", parts.unresolved},
                       {"facePart", qs(facts.face_part)},
                       {"faceVertices", facts.face_vertices ? QVariant(static_cast<qulonglong>(*facts.face_vertices)) : QVariant()}};
    QVariantList ranked;
    for (const auto& r : match.ranked) {
      if (ranked.size() >= 10) break;
      const auto& h = index->heads[r.index];
      ranked << QVariantMap{{"index", static_cast<int>(r.index)}, {"path", h.path}, {"mod", h.mod}, {"score", r.score}, {"overlap", r.overlap},
                            {"common", r.common}, {"exactName", r.exact_name}, {"nearName", r.near_name}, {"reason", qs(r.reason)},
                            {"modified", iso_of(h.modified)}};
    }
    report["ranked"] = ranked;
    if (match.chosen) {
      const auto& h = index->heads[*match.chosen];
      QVariantMap chosen{{"index", static_cast<int>(*match.chosen)}, {"path", h.path}, {"mod", h.mod}, {"modified", iso_of(h.modified)}, {"readable", h.readable}};
      for (std::size_t s = 0; s < h.shapes.size(); ++s) {
        if (text::to_lower_ascii(h.shapes[s].name) == face) {
          chosen["faceVertices"] = static_cast<qulonglong>(h.shapes[s].vertices);
          QStringList missing = s < h.missing.size() ? h.missing[s] : QStringList();
          chosen["missingTextures"] = missing;
        }
      }
      report["chosen"] = chosen;
    }
    QVariantList all;
    for (std::size_t i = 0; i < index->heads.size(); ++i) {
      all << QVariantMap{{"index", static_cast<int>(i)}, {"label", index->heads[i].path}, {"readable", index->heads[i].readable}};
    }
    report["allHeads"] = all;
    const auto ms = clock.elapsed();
    report["timings"] = QVariantMap{{"indexMs", index->ms}, {"checkMs", ms}};
    QMetaObject::invokeMethod(
        self.data(),
        [self, serial, report, vertices_found, vertices_attempted, ms]() mutable {
          if (!self || serial != self->check_serial_) return;
          if (self->parts_ && vertices_attempted && !self->parts_->vertices_done) {
            self->parts_->vertices_done = true;
            if (vertices_found) self->parts_->facts.face_vertices = vertices_found;
          }
          ++self->checks_;
          self->last_check_ms_ = ms;
          self->max_check_ms_ = std::max(self->max_check_ms_, ms);
          self->set_report(std::move(report));
        },
        Qt::QueuedConnection);
  });
}

bool PresetHeadChecker::choose(int index) {
  if (!index_ || index < 0 || static_cast<std::size_t>(index) >= index_->heads.size() || current_path_.isEmpty() || !parts_) return false;
  manual_.insert(current_path_.toLower(), index_->heads[static_cast<std::size_t>(index)].path);
  chosen_at_.insert(current_path_.toLower(), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
  persist_error_ = save_choices();
  ++check_serial_;
  run_match();
  return true;
}

void PresetHeadChecker::clearChoice() {
  if (current_path_.isEmpty() || !manual_.contains(current_path_.toLower())) return;
  manual_.remove(current_path_.toLower());
  chosen_at_.remove(current_path_.toLower());
  persist_error_ = save_choices();
  ++check_serial_;
  run_match();
}

void PresetHeadChecker::reloadChoices() {
  if (store_path_.isEmpty()) return;
  manual_.clear();
  chosen_at_.clear();
  store_unreadable_ = false;
  persist_error_.clear();
  load_choices();
  if (!current_path_.isEmpty() && parts_) {
    ++check_serial_;
    run_match();
  }
}

QVariantMap PresetHeadChecker::metrics() const {
  return {{"checks", checks_}, {"lastCheckMs", last_check_ms_}, {"maxCheckMs", max_check_ms_}, {"lastIndexMs", last_index_ms_},
          {"manualChoices", static_cast<int>(manual_.size())}};
}

}  // namespace sns::app
