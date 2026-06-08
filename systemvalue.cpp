#include "systemvalue.h"

#include "utils.h"

namespace control2 {
SystemValue::SystemValue(QString const &name, float float_value, QObject *parent)
    : QObject{parent}
    , m_name{name}
    , m_label{""}
    , m_value{float_value}
    , m_min{0.0}
    , m_max{0.0}
    , m_prec{1}
    , m_userData{0}
    , m_isSciNotation{false} {
  // setInvalid();
}

SystemValue::SystemValue(QVariantMap const &variantMap, QObject *parent)
    : m_name{""}, m_label{""}, m_value{0.0}, m_min{0.0}, m_max{0.0}, m_prec{1}, m_userData{0}, m_isSciNotation{false} {
  if (!variantMap.empty()) {
    if (variantMap.contains("name")) {
      m_name = variantMap["name"].toString();
    }
    if (variantMap.contains("label")) {
      m_label = variantMap["label"].toString();
    }
    if (variantMap.contains("value")) {
      m_value = variantMap["value"].toFloat();
    }
    if (variantMap.contains("min")) {
      m_min = variantMap["min"].toFloat();
    }
    if (variantMap.contains("max")) {
      m_max = variantMap["max"].toFloat();
    }
    if (variantMap.contains("prec")) {
      m_prec = variantMap["prec"].toFloat();
    }
    if (variantMap.contains("userData")) {
      m_userData = variantMap["userData"].toUInt();
    }
    if (variantMap.contains("notation")) {
      m_isSciNotation = variantMap["notation"].toString() == "scientific";
    }
  }
}

void SystemValue::setValue(float const newValue, bool shadow) noexcept {
  if (newValue == m_value) {
    return;
  }
  m_value = newValue;
  if (!shadow) {
    emit valueChanged(m_value);
  }
}

void SystemValue::notify() {
  emit valueChanged(m_value);
}

void SystemValue::setInvalid() noexcept {
  setValue(utils::floatNan);
}

bool SystemValue::isValid() const noexcept {
  return utils::floatNan == m_value;
}

bool SystemValue::isSciNotation() const noexcept {
  return m_isSciNotation;
}

void SystemValue::setBitOfFloat(quint8 position, bool bit_value, bool shadow) {
  float tmp{getValue()};
  utils::setBitOfFloat(tmp, position, bit_value);

  setValue(tmp, shadow);
}

bool control2::SystemValue::getBitOfFloat(quint8 position) const {
  return utils::getBitOfFloat(m_value, position);
}

void SystemValue::setBitOfUInt(quint8 position, bool bit_value, bool shadow) {
  quint32 tmp{static_cast<quint32>(m_value)};
  utils::setBitOfDword(tmp, position, bit_value);

  setValue(static_cast<float>(tmp), shadow);
}

bool SystemValue::getBitOfUInt(quint8 position) const {
  return utils::getBitOfDword(static_cast<quint32>(m_value), position);
}

QVariantMap SystemValue::toVariant() const {
  QVariantMap ret;
  ret.insert("name", m_name);
  ret.insert("label", m_label);
  ret.insert("value", m_value);
  ret.insert("min", m_min);
  ret.insert("max", m_max);
  ret.insert("prec", m_prec);
  if (m_isSciNotation) {
    ret.insert("notation", "scientific");
  }
  ret.insert("userData", m_userData);
  return ret;
}

} // namespace control2
