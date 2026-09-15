#ifndef STOPBUTTON_H
#define STOPBUTTON_H

#include <QPushButton>

namespace deepvaccontrol {
class StopButton : public QPushButton {
  Q_OBJECT
public:
  explicit StopButton(QWidget *parent = nullptr);
  ~StopButton() override = default;

signals:
  void confirmPressed();
};
} // namespace deepvaccontrol

#endif // STOPBUTTON_H
