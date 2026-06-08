#ifndef STATUSLABEL_H
#define STATUSLABEL_H

#include <QLabel>

#include "resholder.h"

namespace control2 {
class StatusLabel : public QLabel {
  Q_OBJECT
public:
  StatusLabel(ResHolder *resHolder, QWidget *parent = nullptr);
  ~StatusLabel() {
  }
  void setStates(SystemValueList *states);

private:
  void updateString();

  QString    m_statusStr;
  QString    m_typeStr;
};
} // namespace control2

#endif // STATUSLABEL_H
