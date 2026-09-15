#include "systemvaluelist.h"

#include <QDebug>
namespace deepvaccontrol {
SystemValueList::SystemValueList(Logger *logger, QObject *parent) : QObject{parent}, m_logger{logger} {
}

void SystemValueList::add(SystemValue *sysValue) {
  auto name = sysValue->getName();
  if (m_valueMap.contains(name)) {
    QString warString{QString("Try adding existing name '%1' to SystemValueList").arg(name)};
    if (m_logger) {
      m_logger->stream(Logger::Level::WAR) << warString;
    } else {
      qWarning() << warString;
    }

    return;
  }
  sysValue->setParent(this);
  m_valueMap[name] = sysValue;
}

void SystemValueList::add(QString const &name, QString const &label) {
  auto *newSysValue = new SystemValue(name);
  newSysValue->setLabel(label);
  add(newSysValue);
}

void SystemValueList::setValuesOrder(QStringList const &newNameOrderList) {
  m_nameOrderList = newNameOrderList;
}

bool SystemValueList::setFloatValues(QVector<float> const &floatValues) {
  Q_ASSERT(floatValues.size() == m_nameOrderList.size());

  for (int i{0U}; i < m_nameOrderList.size(); ++i) {
    auto const &nameInOrder = m_nameOrderList[i];
    m_valueMap[nameInOrder]->setValue(floatValues[i]);
  }

  emit ValuesUpdated();
  return true;
}

bool SystemValueList::addWithVariantList(QVariantList const &variantList) {
  if (variantList.isEmpty()) {
    return false;
  }

  QStringList newNameOrderList;
  for (auto const &itemVariant : variantList) {
    auto *newSysValue = new SystemValue(itemVariant.toMap(), this);
    add(newSysValue);
    newNameOrderList.push_back(newSysValue->getName()); // set values order as in given varaint list
  }
  setValuesOrder(newNameOrderList);

  return true;
}

QVariantList SystemValueList::getDataVaraint() const {
  QVariantList ret;

  for (auto const &name : m_nameOrderList) {
    auto *sysValue = m_valueMap[name];
    ret.push_back(sysValue->toVariant());
  }

  return ret;
}

bool SystemValueList::contains(QString const &name) const {
  return m_valueMap.contains(name);
}

SystemValue *SystemValueList::getSysValue(QString const &name) const {
  if (m_valueMap.contains(name)) {
    return m_valueMap[name];
  } else {
    QString warString{QString("No state with name '%1'").arg(name)};
    if (m_logger) {
      m_logger->stream(Logger::Level::ERR) << warString;
    } else {
      qCritical() << warString;
    }
    return nullptr;
  }
}
} // namespace deepvaccontrol
