#ifndef POWERBUTTON_H
#define POWERBUTTON_H

#include <QToolButton>

#include "resholder.h"

namespace deepvaccontrol {
// sidebar item: a power icon (tooltip "Off"); asks whether to turn the chamber off and closes the application
class PowerButton : public QToolButton {
  Q_OBJECT
public:
  explicit PowerButton(ResHolder *resHolder, QWidget *parent = nullptr);

signals:
  void prepareToCloseApp();
};
}

#endif // POWERBUTTON_H
