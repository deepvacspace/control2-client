#include "screenswidget.h"

namespace control2{
ScreensWidget::ScreensWidget(QWidget *parent) : QWidget{parent} {
}

void ScreensWidget::selectItem(int index) {
  m_widgets->setCurrentIndex(index);
}
}
