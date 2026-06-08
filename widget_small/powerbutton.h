#ifndef POWERBUTTON_H
#define POWERBUTTON_H

#include <QPushButton>

#include "resholder.h"

namespace control2 {
class PowerButton : public QPushButton {
  Q_OBJECT
public:
  explicit PowerButton(ResHolder *resHolder, QWidget *parent = nullptr);

signals:
  void prepareToCloseApp();
};
} // namespace control2

#endif // POWERBUTTON_H
