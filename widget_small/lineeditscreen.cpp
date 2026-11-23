#include "lineeditscreen.h"

#include <QMouseEvent>

#include "touchsupport.h"

namespace deepvaccontrol {
LineEditScreen::LineEditScreen(QWidget *parent, VirtualKeyboard::Language lang) : QLineEdit{parent}, m_lang{lang} {
}

void LineEditScreen::mousePressEvent(QMouseEvent *event) {
  m_pressAt = event->globalPosition().toPoint();
}

void LineEditScreen::mouseReleaseEvent(QMouseEvent *event) {
  if (!isEnabled() || ((event->globalPosition().toPoint() - m_pressAt).manhattanLength() > touch::tapSlop())) {
    return;
  }

  auto *virtualBoard = new VirtualKeyboard(tr("Text input"), m_lang, this);

  connect(virtualBoard, &VirtualKeyboard::finished, this, [this] {
    this->clearFocus();
  });
  virtualBoard->open();
}
}
