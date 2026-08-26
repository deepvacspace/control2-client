#include "aiwidget.h"

#include <QApplication>
#include <QPalette>
#include <QTabWidget>
#include <QVBoxLayout>

#include "aiadvisorwidget.h"
#include "aisimulatorwidget.h"
#include "utils.h"

namespace control2 {

namespace {
char const AI_TAB_PAGE_OBJECT_NAME[]{"aiTabPage"};

QString aiTabStyleSheet() {
  QString styleSheet =
    utils::readStyleFromFile(":/styles/tabBar.qss").arg(40 * utils::ssf()).arg(100 * utils::ssf()).arg(100 * utils::ssf());

  QPalette const palette  = QApplication::palette();
  QColor const   tabColor = palette.color(QPalette::Button);
  if (tabColor.lightness() < 128) {
    styleSheet += QString("\n"
                          "QTabWidget::pane {"
                          "background-color: %1;"
                          "}\n"
                          "QWidget#%2 {"
                          "background-color: %1;"
                          "color: white;"
                          "}\n"
                          "QWidget#%2 QLabel {"
                          "color: white;"
                          "}")
                    .arg(tabColor.name(), AI_TAB_PAGE_OBJECT_NAME);
  }

  return styleSheet;
}

void prepareAiTabPage(QWidget *widget) {
  widget->setObjectName(AI_TAB_PAGE_OBJECT_NAME);
}
} // namespace

AiWidget::AiWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_tabWidget = new QTabWidget(this);
  m_tabWidget->setStyleSheet(aiTabStyleSheet());
  mainLay->addWidget(m_tabWidget);

  auto *simulatorWidget = new AiSimulatorWidget(resHolder, this);
  prepareAiTabPage(simulatorWidget);
  m_tabWidget->addTab(simulatorWidget, tr("Simulator"));

  auto *advisorWidget = new AiAdvisorWidget(resHolder, this);
  prepareAiTabPage(advisorWidget);
  m_tabWidget->addTab(advisorWidget, tr("Advisor"));
}

} // namespace control2
