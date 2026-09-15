#ifndef SYSTEMVALUELIST_H
#define SYSTEMVALUELIST_H

#include <QMap>
#include <QObject>
#include <QVariantList>

#include "logger.h"
#include "systemvalue.h"

namespace deepvaccontrol {
/**
 * @brief Represent group of SystemValue:
 * - all system states;
 * - all configurable system parameters.
 */
class SystemValueList : public QObject {
  Q_OBJECT
public:
  explicit SystemValueList(Logger *logger = nullptr, QObject *parent = nullptr);
  ~SystemValueList() {
  }

  void         add(QString const &name, QString const &label);
  void         add(SystemValue *sysValue);
  bool         addWithVariantList(QVariantList const &variantList);
  QVariantList getDataVaraint() const;
  bool         contains(QString const &name) const;
  SystemValue *getSysValue(QString const &name) const;
  int          size() const;

  QStringList  getNames() const;
  void         setValuesOrder(QStringList const &newNameOrderList);
  bool         setFloatValues(QVector<float> const &floatValues);

signals:
  void ValuesUpdated();

private:
  Logger                      *m_logger;
  QStringList                  m_nameOrderList;
  QMap<QString, SystemValue *> m_valueMap;
};

inline int SystemValueList::size() const {
  return m_valueMap.size();
}

inline QStringList SystemValueList::getNames() const {
  return m_nameOrderList;
}
} // namespace deepvaccontrol

#endif // SYSTEMVALUELIST_H
