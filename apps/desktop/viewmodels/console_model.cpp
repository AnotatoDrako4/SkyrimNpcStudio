#include "console_model.hpp"

namespace sns::app {

int ConsoleModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(lines_.size());
}

QVariant ConsoleModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= static_cast<int>(lines_.size())) return {};
  const auto& line = lines_[static_cast<size_t>(index.row())];
  switch (role) {
    case TimeRole: return line.time.toString("HH:mm:ss");
    case LevelRole: return line.level;
    case SourceRole: return line.source;
    case TextRole:
    case Qt::DisplayRole: return line.text;
    default: return {};
  }
}

QHash<int, QByteArray> ConsoleModel::roleNames() const {
  return {{TimeRole, "time"}, {LevelRole, "level"}, {SourceRole, "source"}, {TextRole, "message"}};
}

void ConsoleModel::append(const QString& level, const QString& source, const QString& text) {
  if (lines_.size() >= kMaxLines) {
    beginRemoveRows({}, 0, 0);
    lines_.pop_front();
    endRemoveRows();
  }
  const int row = static_cast<int>(lines_.size());
  beginInsertRows({}, row, row);
  lines_.push_back({QDateTime::currentDateTime(), level, source, text});
  endInsertRows();
  if (level == "error") ++errors_;
  if (level == "warning") ++warnings_;
  if (level == "error" || level == "warning") emit countsChanged();
}

void ConsoleModel::clear() {
  beginResetModel();
  lines_.clear();
  endResetModel();
  errors_ = warnings_ = 0;
  emit countsChanged();
}

QString ConsoleModel::allText() const {
  QStringList out;
  for (const auto& l : lines_) out << QString("%1 [%2] %3: %4").arg(l.time.toString(Qt::ISODate), l.level, l.source, l.text);
  return out.join('\n');
}

}  // namespace sns::app
