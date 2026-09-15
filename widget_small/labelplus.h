#ifndef LABELPLUS_H
#define LABELPLUS_H

#include <QLabel>

#include "systemvalue.h"

namespace deepvaccontrol {
class LabelPlus : public QLabel {
  Q_OBJECT
public:
  LabelPlus(QWidget *parent = nullptr);
  LabelPlus(SystemValue *sysValue, QWidget *parent = nullptr);
  LabelPlus(SystemValue *sysValue, QString (*df)(float, int), QWidget *parent = nullptr);
  ~LabelPlus();
  void setSysValue(SystemValue *sysValue);
  void setPrec(int newPrec = 0);
  void setFormat(QString (*dispFnc)(float val, int prec));

private:
  void         updateValue();
  SystemValue *m_sysValue;
  int          m_prec;
  QString      (*m_dispFnc)(float val, int prec);
};

inline void LabelPlus::setPrec(int newPrec) {
  m_prec = newPrec;
}

inline void LabelPlus::setFormat(QString (*dispFnc)(float, int)) {
  m_dispFnc = dispFnc;
}
} // namespace deepvaccontrol

#endif // LABELPLUS_H
