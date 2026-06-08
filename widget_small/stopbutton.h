#ifndef STOPBUTTON_H
#define STOPBUTTON_H

#include <QPushButton>

namespace control2 {
class StopButton : public QPushButton {
  Q_OBJECT
public:
  explicit StopButton(QWidget *parent = nullptr);
  ~StopButton() override = default;

signals:
  void confirmPressed();
};
} // namespace control2

#endif // STOPBUTTON_H
