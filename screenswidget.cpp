#include "screenswidget.h"

namespace deepvaccontrol{
ScreensWidget::ScreensWidget(QWidget *parent) : QWidget{parent} {
}

void ScreensWidget::selectItem(int index) {
  m_widgets->setCurrentIndex(index);
}
}
