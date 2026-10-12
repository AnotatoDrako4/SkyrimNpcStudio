#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace sns::app {

// A list model over QVariantMap rows whose keys become QML roles. Used for worker results
// (NPC search, armor catalog, mods) so QML delegates can use `model.name`, `model.formKey`…
class VariantListModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by view models")
  Q_PROPERTY(int count READ count NOTIFY countChanged)

 public:
  VariantListModel(QStringList roles, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void reset(QList<QVariantMap> rows);
  void append(const QList<QVariantMap>& rows);
  [[nodiscard]] int count() const { return static_cast<int>(rows_.size()); }
  Q_INVOKABLE QVariantMap get(int row) const;

 signals:
  void countChanged();

 private:
  QStringList roles_;
  QList<QVariantMap> rows_;
};

}  // namespace sns::app
