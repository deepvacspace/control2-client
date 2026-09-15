#include "scrollwidget.h"

#include <QScrollArea>
#include <QVBoxLayout>

#include "utils.h"

namespace deepvaccontrol {
ScrollWidget::ScrollWidget(bool useStyle, QWidget *parent) : QWidget{parent} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(3, 3, 3, 3);

  m_marginWidget = new QWidget(this);

  auto *marginWidgetScroll = new QScrollArea();
  marginWidgetScroll->setWidget(m_marginWidget);
  marginWidgetScroll->setWidgetResizable(true);
  marginWidgetScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  if (useStyle) {
    marginWidgetScroll->setStyleSheet(
      marginWidgetScroll->styleSheet() + utils::readStyleFromFile(":/styles/verticalScroll.qss"));
  }
  mainLay->addWidget(marginWidgetScroll);
}

void ScrollWidget::setLayout2(QLayout *layout) {
  m_marginWidget->setLayout(layout);
}
} // namespace deepvaccontrol
