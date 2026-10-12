#include "character_forge_viewmodel.hpp"

#include <QRegularExpression>
#include <QUrl>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace sns::app {
namespace {

QString local_path(const QString& path) {
  const QUrl url(path);
  return url.isLocalFile() ? url.toLocalFile() : path;
}

QString safe_stem(QString name) {
  name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), "_");
  return name.isEmpty() ? QStringLiteral("CharacterForge") : name.left(80);
}

}  // namespace

CharacterForgeViewModel::CharacterForgeViewModel(EnvironmentProvider environment, QObject* parent)
    : QObject(parent), environment_(std::move(environment)) {}

QString CharacterForgeViewModel::project_name() const {
  return has_project_ ? QString::fromStdString(draft_.current().name) : QString();
}
QString CharacterForgeViewModel::character_name() const {
  return has_project_ ? QString::fromStdString(draft_.current().character_name) : QString();
}
QString CharacterForgeViewModel::editor_id() const {
  return has_project_ ? QString::fromStdString(draft_.current().editor_id) : QString();
}
QString CharacterForgeViewModel::description() const {
  return has_project_ ? QString::fromStdString(draft_.current().description) : QString();
}

bool CharacterForgeViewModel::newProject(const QString& name) {
  if (dirty()) {
    last_error_ = tr("Guarda el proyecto actual antes de crear otro.");
    emit changed();
    return false;
  }
  try {
    draft_.replace(creation::ForgeProjectStore::create(name.toStdString(), environment_()));
    has_project_ = true;
    project_path_.clear();
    last_error_.clear();
    emit changed();
    return true;
  } catch (const std::exception& ex) {
    last_error_ = QString::fromUtf8(ex.what());
    emit changed();
    return false;
  }
}

bool CharacterForgeViewModel::openProject(const QString& folder_or_url) {
  if (dirty()) {
    last_error_ = tr("Guarda el proyecto actual antes de abrir otro.");
    emit changed();
    return false;
  }
  try {
    const auto folder = std::filesystem::path(local_path(folder_or_url).toStdWString());
    auto project = creation::ForgeProjectStore::load(folder);
    draft_.replace(std::move(project));
    has_project_ = true;
    project_path_ = QString::fromStdWString(folder.wstring());
    last_error_.clear();
    emit changed();
    return true;
  } catch (const std::exception& ex) {
    last_error_ = QString::fromUtf8(ex.what());
    emit changed();
    return false;
  }
}

bool CharacterForgeViewModel::saveProject(const QString& folder_or_url) {
  if (!has_project_) return false;
  try {
    auto folder = std::filesystem::path(local_path(folder_or_url).toStdWString());
    if (folder.extension() == L".snps")
      throw std::invalid_argument("Cannot create Character Forge inside an existing .snps project");
    if (folder.extension() != L".sncf") {
      folder /= (safe_stem(project_name()) + QStringLiteral(".sncf")).toStdWString();
    }
    creation::ForgeProjectStore::save(folder, draft_.current());
    draft_.mark_saved();
    project_path_ = QString::fromStdWString(folder.wstring());
    last_error_.clear();
    emit changed();
    return true;
  } catch (const std::exception& ex) {
    last_error_ = QString::fromUtf8(ex.what());
    emit changed();
    return false;
  }
}

template <class F> void CharacterForgeViewModel::edit(F&& change) {
  if (!has_project_) return;
  try {
    change();
    last_error_.clear();
  } catch (const std::exception& ex) {
    last_error_ = QString::fromUtf8(ex.what());
  }
  emit changed();
}

void CharacterForgeViewModel::setProjectName(const QString& value) {
  edit([&] { draft_.set_project_name(value.toStdString()); });
}
void CharacterForgeViewModel::setCharacterName(const QString& value) {
  edit([&] { draft_.set_character_name(value.toStdString()); });
}
void CharacterForgeViewModel::setEditorId(const QString& value) {
  edit([&] { draft_.set_editor_id(value.toStdString()); });
}
void CharacterForgeViewModel::setDescription(const QString& value) {
  edit([&] { draft_.set_description(value.toStdString()); });
}
void CharacterForgeViewModel::undo() {
  if (has_project_ && draft_.undo()) emit changed();
}
void CharacterForgeViewModel::redo() {
  if (has_project_ && draft_.redo()) emit changed();
}

}  // namespace sns::app
