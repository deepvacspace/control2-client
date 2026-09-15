#ifndef LINEEDITSCREEN_H
#define LINEEDITSCREEN_H

#include <QLineEdit>

#include "virtualkeyboard.h"

namespace deepvaccontrol {
class LineEditScreen : public QLineEdit {
  Q_OBJECT
public:
  explicit LineEditScreen(QWidget *parent = nullptr, VirtualKeyboard::Language lang = VirtualKeyboard::RU);

private:
  VirtualKeyboard::Language m_lang;

  // QWidget interface
protected:
  void mousePressEvent(QMouseEvent *event) override;
};
} // namespace deepvaccontrol

#endif // LINEEDITSCREEN_H
