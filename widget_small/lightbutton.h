#ifndef LIGHTBUTTON_H
#define LIGHTBUTTON_H

#include <QPushButton>

#include "gatetcp.h"
#include "systemvalue.h"

namespace control2 {
class LightButton : public QPushButton {
  Q_OBJECT
public:
  explicit LightButton(GateTcp *gate, SystemValue *sysVal, QWidget *parent = nullptr);

private:
  GateTcp     *m_gate;
  SystemValue *m_sysVal;
  bool         m_enabled;
};
} // namespace control2

#endif // LIGHTBUTTON_H
