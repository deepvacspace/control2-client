#include "flasher.h"

#include <QColor>
#include <QEvent>
#include <QWidget>

namespace control2 {
Flasher::Flasher(QWidget *parent) : QLabel{parent}, m_flashingEnable{false} {
  setAttribute(Qt::WA_TransparentForMouseEvents); // Игнорирует клики, передавая их подлежащим элементам
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  if (parent) {
    parent->installEventFilter(this); // Устанавливаем фильтр событий на родителя
    this->resize(parent->size());     // Первоначальный размер
  }

  // Настраиваем таймер на 50 мс (~20 кадров в секунду)
  connect(&m_timer, &QTimer::timeout, this, [this]() {
    m_elapsedTime += 1;
    update(); // Перерисовка круга с новой прозрачностью
  });
}

void Flasher::setFlashing(bool enable) {
  m_flashingEnable = enable;
  if (m_flashingEnable) {
    m_timer.start(50);
  } else {
    m_timer.stop();
    update();
  }
}

void Flasher::paintEvent(QPaintEvent *event) {
  if (m_flashingEnable) {
    int const alpha{static_cast<int>(80 + 80 * qSin(m_elapsedTime * 0.3))};

    // Set brush with transparency
    QColor const redTransparent(255, 0, 0, alpha);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(redTransparent);
    painter.setPen(Qt::NoPen);
    painter.drawRect(0, 0, width(), height());
  }
}

void Flasher::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  setGeometry(0, 0, parentWidget()->width(), parentWidget()->height()); // Обновляем размер при изменении окна
}

bool Flasher::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::Resize) {
    if (QWidget *parent = parentWidget()) {
      this->resize(parent->size()); // Подстраиваем размер под родителя
    }
  }
  return QLabel::eventFilter(watched, event);
}
} // namespace control2
