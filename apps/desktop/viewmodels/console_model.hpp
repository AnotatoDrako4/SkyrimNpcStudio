#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QtQml/qqmlregistration.h>
#include <deque>

namespace sns::app {

// Log lines from the studio and the worker, shown in the bottom console panel.
class ConsoleModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Owned by StudioController")
  Q_PROPERTY(int errorCount READ error_count NOTIFY countsChanged)
  Q_PROPERTY(int warningCount READ warning_count NOTIFY countsChanged)

 public:
  enum Role { TimeRole = Qt::UserRole + 1, LevelRole, SourceRole, TextRole };
  static constexpr int kMaxLines = 5000;

  using QAbstractListModel::QAbstractListModel;

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void append(const QString& level, const QString& source, const QString& text);
  Q_INVOKABLE void clear();
  Q_INVOKABLE QString allText() const;

  [[nodiscard]] int error_count() const { return errors_; }
  [[nodiscard]] int warning_count() const { return warnings_; }

 signals:
  void countsChanged();

 private:
  struct Line {
    QDateTime time;
    QString level;
    QString source;
    QString text;
  };
  std::deque<Line> lines_;
  int errors_ = 0;
  int warnings_ = 0;
};

}  // namespace sns::app
