#include "forge_skyrim_package.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <set>

#include "sns/creation/skeleton_mapping.hpp"
#include "sns/creation/skyrim_conversion.hpp"
#include "sns/textures/dds.hpp"

namespace sns::app::forge_skyrim {
namespace {

using Json = nlohmann::json;

QVariantMap to_map(const Json& value) {
  return QJsonDocument::fromJson(QByteArray::fromStdString(value.dump())).object().toVariantMap();
}

QByteArray read_file(const QString& path, qint64 limit) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > limit) throw std::runtime_error("Cannot read " + path.toStdString());
  return file.readAll();
}

void write_file(const QString& path, const QByteArray& bytes) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) throw std::runtime_error("Cannot create " + path.toStdString());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(bytes) != bytes.size() || !file.flush())
    throw std::runtime_error("Cannot write " + path.toStdString());
}

// A path from the Blender report: relative, inside the stage and under the expected Data subfolder.
QString staged_path(const QString& stage, const std::string& relative, const QString& under) {
  const auto rel = QDir::fromNativeSeparators(QString::fromStdString(relative));
  if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel.contains("..") || !rel.startsWith(under, Qt::CaseInsensitive))
    throw std::invalid_argument("Unsafe path in the Blender report: " + relative);
  const auto path = QDir::cleanPath(stage + "/" + rel);
  if (!path.startsWith(QDir::cleanPath(stage) + "/") || !QFileInfo(path).isFile())
    throw std::invalid_argument("Missing staged file: " + relative);
  return path;
}

QByteArray encode_texture(const QString& png) {
  QImage image(png);
  if (image.isNull() || image.width() <= 0 || image.width() > 8192 || image.height() > 8192)
    throw std::runtime_error("Cannot decode staged texture " + png.toStdString());
  image = image.convertToFormat(QImage::Format_RGBA8888);
  textures::DdsImage rgba;
  rgba.width = static_cast<std::uint32_t>(image.width());
  rgba.height = static_cast<std::uint32_t>(image.height());
  rgba.format = textures::TextureFormat::RGBA8;
  rgba.has_alpha = true;
  rgba.data.resize(static_cast<std::size_t>(image.width()) * image.height() * 4);
  for (int y = 0; y < image.height(); ++y)
    std::memcpy(rgba.data.data() + static_cast<std::size_t>(y) * image.width() * 4, image.constScanLine(y),
                static_cast<std::size_t>(image.width()) * 4);
  const auto format = textures::measure_alpha(rgba).uses_alpha ? textures::TextureFormat::BC3 : textures::TextureFormat::BC1;
  return QByteArray::fromStdString(textures::encode_dds(rgba, format, true));
}

}  // namespace

QString slug(const QString& text) {
  QString out;
  for (const auto c : text.toLower()) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    else if (!out.endsWith('_')) out += '_';
  }
  while (out.startsWith('_')) out.remove(0, 1);
  out = out.left(48);
  while (out.endsWith('_')) out.chop(1);
  return out.isEmpty() ? QStringLiteral("model") : out;
}

QVariantMap package_export(const QString& project_folder, const QString& stage, const Json& report, const QString& name) {
  const QFileInfo project(project_folder);
  if (project.suffix() != "sncf" || !QFileInfo(project_folder + "/project.json").isFile())
    throw std::invalid_argument("Save the .sncf project before exporting to Skyrim");
  if (report.value("schema", "") != creation::skyrim::kReportSchema || report.value("operation", "") != "exportNif")
    throw std::invalid_argument("Not an exportNif report");

  const auto profile = report.value("profile", std::string("static"));
  if (profile != "static" && profile != "skinned") throw std::invalid_argument("Unknown conversion profile in the report");
  const auto classification = creation::skyrim::classify(report, profile);
  const auto nif_path = staged_path(stage, report.at("export").at("output").get<std::string>(), "Data/meshes/");
  const auto nif = read_file(nif_path, 64LL * 1024 * 1024);
  const auto verification = creation::skyrim::verify_nif(std::string_view(nif.constData(), nif.size()), report, profile);
  QVariantMap result{{"packaged", false}, {"verification", to_map(verification)}, {"classification", to_map(classification)},
                     {"profile", QString::fromStdString(profile)}, {"skyrimStatus", "PENDIENTE_SKYRIM"}};
  if (profile == "skinned") result["states"] = to_map(creation::skyrim::skinned_states(report, classification, &verification));
  if (!classification.at("exportable").get<bool>() || !verification.at("passed").get<bool>()) return result;

  const auto exports = QDir::cleanPath(project.absoluteFilePath() + "/exports/skyrim");
  if (!QDir().mkpath(exports)) throw std::runtime_error("Cannot create the exports folder of the project");
  const auto stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss");
  const auto final_folder = exports + "/" + slug(name) + "-" + stamp;
  if (QFileInfo::exists(final_folder)) throw std::runtime_error("An export with the same name and second already exists");
  const auto partial = exports + "/.partial-" + QUuid::createUuid().toString(QUuid::WithoutBraces);

  Json files = Json::array();
  const auto add = [&](const QString& relative, const QByteArray& bytes) {
    write_file(partial + "/" + relative, bytes);
    files.push_back({{"path", relative.toStdString()}, {"bytes", bytes.size()},
                     {"sha256", QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toUpper().toStdString()}});
  };
  try {
    add(QDir::cleanPath(QDir(stage).relativeFilePath(nif_path)), nif);
    for (const auto& texture : report.at("prepare").at("textures").value("staged", Json::array())) {
      const auto png = staged_path(stage, texture.at("png").get<std::string>(), "Data/textures/");
      const auto nif_texture = QString::fromStdString(texture.at("nifPath").get<std::string>()).replace('\\', '/');
      if (!nif_texture.startsWith("textures/") || nif_texture.contains("..") || !nif_texture.endsWith(".dds"))
        throw std::invalid_argument("Unsafe texture path in the Blender report");
      add("Data/" + nif_texture, encode_texture(png));
    }
    add("report.json", QByteArray::fromStdString(report.dump(1)));
    add("verification.json", QByteArray::fromStdString(Json{{"classification", classification}, {"nif", verification}}.dump(1)));
    const Json manifest{{"schema", "sns.forge.skyrim.package/1"}, {"name", slug(name).toStdString()},
                        {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()},
                        {"targetGame", "SKYRIMSE"}, {"profile", profile},
                        {"kind", profile == "skinned" ? "SkinnedMesh-NIF" : "TypeA-resource"},
                        {"plugin", nullptr},  // no ESP: placing the resource needs a record a later step creates
                        {"referenceSkeleton", report.value("skeletonMapping", Json::object()).value("reference", Json())},
                        {"blenderVersion", report.value("blenderVersion", "")},
                        {"pyniflyVersion", report.value("pynifly", Json::object()).value("version", Json::array())},
                        {"sourceSha256", report.value("sourceSha256", "")}, {"nifVerified", true},
                        {"skyrimStatus", "PENDIENTE_SKYRIM"}, {"files", files}};
    write_file(partial + "/manifest.json", QByteArray::fromStdString(manifest.dump(1)));
    if (!QDir().rename(partial, final_folder)) throw std::runtime_error("Cannot move the finished export into place");
    result["packaged"] = true;
    result["folder"] = final_folder;
    result["manifest"] = to_map(manifest);
  } catch (...) {
    QDir(partial).removeRecursively();
    throw;
  }
  return result;
}

QString default_reference_skeleton(const Json& report) {
  if (const auto path = qEnvironmentVariable("SNS_FORGE_REFERENCE_SKELETON"); !path.isEmpty()) return path;
  const auto addon = QString::fromStdString(report.value("pynifly", Json::object()).value("path", std::string{}));
  if (addon.isEmpty()) return {};
  const auto path = QDir(addon).filePath("skeletons/Skyrim/skeleton.nif");
  return QFileInfo(path).isFile() ? path : QString();
}

void attach_skeleton_mapping(Json& report, const QString& skeleton_path, const QString& label) {
  report.erase("skeletonMapping");
  if (skeleton_path.isEmpty()) return;
  const auto inspection = report.contains("prepared") ? report.at("prepared") : report.value("inspection", Json::object());
  std::string bound;
  std::set<std::string> weighted;
  for (const auto& mesh : inspection.value("meshes", Json::array())) {
    const auto skin = mesh.value("skin", Json::object());
    if (skin.empty()) continue;
    if (bound.empty()) bound = skin.value("armature", std::string{});
    const auto bones = skin.value("weightedBones", Json::object());
    for (const auto& [bone, count] : bones.items()) weighted.insert(bone);
  }
  if (bound.empty()) return;
  const auto bytes = read_file(skeleton_path, 64LL * 1024 * 1024);  // read only; never copied into a package
  const auto sha = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toUpper().toStdString();
  const auto reference = creation::skyrim::load_reference_skeleton(std::string_view(bytes.constData(), bytes.size()),
                                                                   (label.isEmpty() ? skeleton_path : label).toStdString(), sha);
  for (const auto& armature : inspection.value("armatures", Json::array()))
    if (armature.value("name", std::string{}) == bound) {
      report["skeletonMapping"] = creation::skyrim::map_skeleton(armature, reference, {weighted.begin(), weighted.end()});
      return;
    }
}

Json mod_request(const QString& package_folder, const QString& output_directory) {
  const auto read_json = [&](const QString& name) {
    const auto bytes = read_file(package_folder + "/" + name, 16LL * 1024 * 1024);
    auto json = Json::parse(bytes.toStdString(), nullptr, false);
    if (!json.is_object()) throw std::invalid_argument(name.toStdString() + " of the package is not a JSON object");
    return json;
  };
  const auto manifest = read_json("manifest.json");
  if (manifest.value("schema", "") != "sns.forge.skyrim.package/1" || manifest.value("profile", "") != "static" ||
      manifest.value("nifVerified", false) != true)
    throw std::invalid_argument("Not a verified static Character Forge package");
  const auto nif = read_json("verification.json").value("nif", Json::object());
  if (nif.value("passed", false) != true || !nif.contains("boundsMin") || !nif.contains("boundsMax"))
    throw std::invalid_argument("The package has no verified NIF bounds (export it again)");

  const auto name = slug(QString::fromStdString(manifest.value("name", "")));
  Json files = Json::array();
  QString model;
  for (const auto& f : manifest.value("files", Json::array())) {
    const auto path = QString::fromStdString(f.value("path", ""));
    if (!path.startsWith("Data/")) continue;  // report.json, verification.json: the studio's, not the game's
    const auto relative = path.mid(5);
    files.push_back({{"relativePath", relative.toStdString()}, {"sha256", f.value("sha256", "")}});
    if (relative.startsWith("meshes/") && relative.endsWith(".nif")) {
      if (!model.isEmpty()) throw std::invalid_argument("A static package has exactly one NIF");
      model = relative.mid(7).replace('/', '\\');
    }
  }
  if (model.isEmpty()) throw std::invalid_argument("The package has no NIF");

  Json low = Json::array(), high = Json::array();
  for (int i = 0; i < 3; ++i) {
    const auto lo = nif.at("boundsMin").at(i).get<double>(), hi = nif.at("boundsMax").at(i).get<double>();
    constexpr double limit = std::numeric_limits<short>::max();
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi || std::floor(lo) < -limit || std::ceil(hi) > limit)
      throw std::invalid_argument("The NIF bounds do not fit a plugin's OBND (16-bit game units)");
    low.push_back(static_cast<int>(std::floor(lo)));
    high.push_back(static_cast<int>(std::ceil(hi)));
  }
  const auto id = "CF_" + name.toStdString();
  return {{"pluginName", id + ".esp"},
          {"outputDirectory", QDir::toNativeSeparators(output_directory).toStdString()},
          {"createZip", true},
          {"eslFlag", true},
          {"author", "Skyrim NPC Studio"},
          {"description", "Character Forge static resource " + name.toStdString() + " (PENDIENTE_SKYRIM)"},
          {"editorId", id},
          {"sourceDirectory", QDir::toNativeSeparators(QFileInfo(package_folder).absoluteFilePath()).toStdString()},
          {"files", files},
          {"modelPath", model.toStdString()},
          {"boundsMin", low},
          {"boundsMax", high}};
}

}  // namespace sns::app::forge_skyrim
