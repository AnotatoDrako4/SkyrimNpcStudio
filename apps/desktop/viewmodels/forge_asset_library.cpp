#include "forge_asset_library.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

#include "sns/gltf/gltf_inspect.hpp"

namespace sns::app::forge_assets {
namespace {
namespace fs = std::filesystem;

fs::path path_of(const QString& value) { return fs::path(value.toStdWString()); }
QString string_of(const fs::path& value) { return QString::fromStdWString(value.wstring()); }

gltf::Limits limits() {
  gltf::Limits value;
  value.max_file_bytes = 256ull * 1024 * 1024;
  value.max_resource_bytes = 512ull * 1024 * 1024;
  value.max_vertices = 5'000'000;
  value.max_triangles = 5'000'000;
  return value;
}

void add_bytes(QCryptographicHash& hash, const std::string& bytes) {
  const auto length = QByteArray::number(static_cast<qulonglong>(bytes.size())) + ':';
  hash.addData(length);
  hash.addData(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())));
}

QString digest(const gltf::LoadedAsset& asset) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  add_bytes(hash, asset.main_bytes);
  auto resources = asset.resources;
  std::sort(resources.begin(), resources.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [relative, bytes] : resources) {
    add_bytes(hash, relative);
    add_bytes(hash, bytes);
  }
  return QString::fromLatin1(hash.result().toHex());
}

gltf::LoadedAsset inspected(const fs::path& file) {
  auto asset = gltf::load_and_inspect(file, limits());
  if (!asset.inspection.loadable()) {
    const auto& issues = asset.inspection.issues;
    throw std::runtime_error(issues.empty() ? "Model validation failed" : issues.front().code + ": " + issues.front().message);
  }
  return asset;
}

void write_bytes(const fs::path& file, const std::string& bytes) {
  fs::create_directories(file.parent_path());
  QFile output(string_of(file));
  if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
      output.write(bytes.data(), static_cast<qint64>(bytes.size())) != static_cast<qint64>(bytes.size()) || !output.flush())
    throw std::runtime_error("Cannot write a managed model resource");
}

bool inside(const fs::path& root, const fs::path& candidate) {
  const auto relative = candidate.lexically_relative(root);
  return !relative.empty() && *relative.begin() != L".." && !relative.is_absolute();
}

}  // namespace

ImportedModel import_model(const QString& project_folder, const QString& source_file) {
  const fs::path project = path_of(project_folder);
  if (project.extension() != L".sncf" || !fs::is_regular_file(project / "project.json"))
    throw std::invalid_argument("Save the .sncf project before copying a model into its library");
  const fs::path source = path_of(source_file);
  const auto asset = inspected(source);
  const auto sha = digest(asset);
  const fs::path root = project / "assets" / "models";
  fs::create_directories(root);
  const fs::path canonical_root = fs::canonical(root);
  if (!inside(fs::canonical(project), canonical_root))
    throw std::invalid_argument("The model library escapes the .sncf project");
  const auto extension = source.extension();
  const fs::path relative = fs::path("assets") / "models" / sha.toStdString() / (std::wstring(L"model") + extension.wstring());
  const fs::path destination_folder = project / relative.parent_path();
  const fs::path destination = project / relative;
  if (fs::exists(destination_folder)) {
    if (fs::is_symlink(destination_folder) || !inside(canonical_root, fs::canonical(destination_folder)) ||
        !fs::is_regular_file(destination) || digest(inspected(destination)) != sha)
      throw std::invalid_argument("An existing managed model does not match its SHA-256");
    return {string_of(relative).replace('\\', '/'), sha, static_cast<qint64>(asset.inspection.file_bytes), true};
  }

  const fs::path stage = root / (".stage-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString());
  try {
    fs::create_directory(stage);
    write_bytes(stage / relative.filename(), asset.main_bytes);
    for (const auto& [name, bytes] : asset.resources) {
      if (!gltf::safe_relative_uri(name)) throw std::invalid_argument("Unsafe model resource path");
      write_bytes(stage / fs::path(QString::fromStdString(name).toStdWString()), bytes);
    }
    if (digest(inspected(stage / relative.filename())) != sha)
      throw std::runtime_error("The staged model differs from the validated source");
    fs::rename(stage, destination_folder);
  } catch (...) {
    if (fs::exists(stage) && inside(canonical_root, fs::canonical(stage))) fs::remove_all(stage);
    throw;
  }
  return {string_of(relative).replace('\\', '/'), sha, static_cast<qint64>(asset.inspection.file_bytes), false};
}

QString locate_model(const QString& project_folder, const QString& relative_path) {
  if (project_folder.isEmpty()) return {};
  const auto safe = gltf::safe_relative_uri(relative_path.toStdString());
  if (!safe || safe->rfind("assets/models/", 0) != 0) return {};
  try {
    const fs::path root = fs::canonical(path_of(project_folder));
    const fs::path file = root / fs::path(QString::fromStdString(*safe).toStdWString());
    if (!fs::is_regular_file(file) || fs::is_symlink(file)) return {};
    const auto resolved = fs::canonical(file);
    if (!inside(root, resolved)) return {};
    return string_of(resolved);
  } catch (...) { return {}; }
}

QString resolve_model(const QString& project_folder, const QString& relative_path, const QString& sha256) {
  if (sha256.size() != 64) return {};
  const auto path = locate_model(project_folder, relative_path);
  if (path.isEmpty()) return {};
  try { return digest(inspected(path_of(path))) == sha256 ? path : QString(); }
  catch (...) { return {}; }
}

}  // namespace sns::app::forge_assets
