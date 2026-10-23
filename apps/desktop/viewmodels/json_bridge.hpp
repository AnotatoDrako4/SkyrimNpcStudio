#pragma once

#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QString>
#include <QVariant>
#include <nlohmann/json.hpp>

// Conversions between the engines' nlohmann::json and Qt/QML types. Strings are UTF-8 on both sides.
namespace sns::app {

using Json = nlohmann::json;

inline QString qs(const std::string& s) { return QString::fromStdString(s); }
inline std::string ss(const QString& s) { return s.toStdString(); }

inline QVariant to_variant(const Json& j) {
  switch (j.type()) {
    case Json::value_t::object:
    case Json::value_t::array:
      return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump())).toVariant();
    case Json::value_t::boolean: return j.get<bool>();
    case Json::value_t::number_integer: return QVariant::fromValue<qlonglong>(j.get<long long>());
    case Json::value_t::number_unsigned: return QVariant::fromValue<qulonglong>(j.get<unsigned long long>());
    case Json::value_t::number_float: return j.get<double>();
    case Json::value_t::string: return qs(j.get<std::string>());
    default: return {};
  }
}

inline QVariantMap to_map(const Json& j) { return j.is_object() ? to_variant(j).toMap() : QVariantMap{}; }
inline QVariantList to_list(const Json& j) { return j.is_array() ? to_variant(j).toList() : QVariantList{}; }

inline Json to_json(const QVariant& v) {
  // A JS array or object handed from QML to a `const QVariant&` parameter can arrive wrapped in a QJSValue, which QJsonValue::fromVariant
  // turns into null (a spell, perk, package or faction list chosen in the UI used to become `null` and the edit was refused).
  const QVariant plain = v.metaType() == QMetaType::fromType<QJSValue>() ? v.value<QJSValue>().toVariant() : v;
  const QJsonArray wrapper{QJsonValue::fromVariant(plain)};
  const auto bytes = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
  return Json::parse(bytes.toStdString())[0];
}

}  // namespace sns::app
