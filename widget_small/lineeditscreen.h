#ifndef LINEEDITSCREEN_H
#define LINEEDITSCREEN_H

#include <QLineEdit>

#include "virtualkeyboard.h"

namespace deepvaccontrol {
class LineEditScreen : public QLineEdit {
  Q_OBJECT
public:
  explicit LineEditScreen(QWidget *parent = nullptr, VirtualKeyboard::Language lang = VirtualKeyboard::EN);

private:
  VirtualKeyboard::Language m_lang;

  // QWidget interface
protected:
  // the keyboard opens on a tap: a finger that moved (scrolling the page) does not open it
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  QPoint m_pressAt;
};
}

#endif // LINEEDITSCREEN_H
