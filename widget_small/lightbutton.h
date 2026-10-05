#ifndef LIGHTBUTTON_H
#define LIGHTBUTTON_H

#include <QPushButton>

#include "resholder.h"

namespace deepvaccontrol {
class LightButton : public QPushButton {
  Q_OBJECT
public:
  // off and disabled unless a light output is configured ("monitor") and the chamber is online
  explicit LightButton(ResHolder *resHolder, QWidget *parent = nullptr);

protected:
  // footer card: lamp, label and an on / off switch
  void paintEvent(QPaintEvent *event) override;

private:
  void         updateAvailability();
  ResHolder   *m_resHolder;
  SystemValue *m_sysVal{nullptr};
  int          m_bit{-1}; // light output bit, -1 when no light is configured
  bool         m_enabled{false};
};
} // namespace deepvaccontrol

#endif // LIGHTBUTTON_H
