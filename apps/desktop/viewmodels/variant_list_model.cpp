#include "variant_list_model.hpp"

namespace sns::app {

VariantListModel::VariantListModel(QStringList roles, QObject* parent)
    : QAbstractListModel(parent), roles_(std::move(roles)) {}

int VariantListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant VariantListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= rows_.size()) return {};
  const int key = role - Qt::UserRole - 1;
  if (key < 0 || key >= roles_.size()) return {};
  // The worker omits null fields; an invalid QVariant would reach QML as the text "undefined".
  const auto value = rows_[index.row()].value(roles_[key]);
  return value.isValid() ? value : QVariant(QString());
}

QHash<int, QByteArray> VariantListModel::roleNames() const {
  QHash<int, QByteArray> names;
  for (int i = 0; i < roles_.size(); ++i) names[Qt::UserRole + 1 + i] = roles_[i].toUtf8();
  return names;
}

void VariantListModel::reset(QList<QVariantMap> rows) {
  beginResetModel();
  rows_ = std::move(rows);
  endResetModel();
  emit countChanged();
}

void VariantListModel::append(const QList<QVariantMap>& rows) {
  if (rows.isEmpty()) return;
  beginInsertRows({}, static_cast<int>(rows_.size()), static_cast<int>(rows_.size() + rows.size() - 1));
  rows_.append(rows);
  endInsertRows();
  emit countChanged();
}

QVariantMap VariantListModel::get(int row) const {
  return row >= 0 && row < rows_.size() ? rows_[row] : QVariantMap{};
}

}  // namespace sns::app
