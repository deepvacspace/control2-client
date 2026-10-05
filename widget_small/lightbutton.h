#ifndef LIGHTBUTTON_H
#define LIGHTBUTTON_H

#include <QPushButton>

#include "gatetcp.h"
#include "systemvalue.h"

namespace deepvaccontrol {
class LightButton : public QPushButton {
  Q_OBJECT
public:
  explicit LightButton(GateTcp *gate, SystemValue *sysVal, QWidget *parent = nullptr);

protected:
  // footer card: lamp, label and an on / off switch
  void paintEvent(QPaintEvent *event) override;

private:
  GateTcp     *m_gate;
  SystemValue *m_sysVal;
  bool         m_enabled{false};
};
} // namespace deepvaccontrol

#endif // LIGHTBUTTON_H
