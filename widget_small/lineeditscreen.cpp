#include "lineeditscreen.h"

namespace deepvaccontrol {
LineEditScreen::LineEditScreen(QWidget *parent, VirtualKeyboard::Language lang) : QLineEdit{parent}, m_lang{lang} {
}

void LineEditScreen::mousePressEvent(QMouseEvent *event) {
  if (!isEnabled()) {
    return;
  }

  auto *virtualBoard = new VirtualKeyboard(tr("Ввод текста"), m_lang, this);

  connect(virtualBoard, &VirtualKeyboard::finished, this, [this] {
    this->clearFocus();
  });
  virtualBoard->open();
}
} // namespace deepvaccontrol
