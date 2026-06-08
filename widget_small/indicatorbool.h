#ifndef INDICATORBOOL_H
#define INDICATORBOOL_H

#include <QFrame>
#include <QLabel>

#include "systemvalue.h"

namespace control2 {
class IndicatorBool : public QFrame {
  Q_OBJECT
public:
  explicit IndicatorBool(SystemValue *sysVal,
                         quint8       bitIndex,
                         QWidget     *parent  = nullptr,
                         QString      textOn  = QString{},
                         QString      textOff = QString{});

private:
  void updateGui();

private:
  SystemValue *m_sysVal;
  quint8       m_bitIndex;
  QString      m_textOn;
  QString      m_textOff;
  QLabel      *m_label;
};
} // namespace control2

#endif // INDICATORBOOL_H
