#include "aiwidget.h"

#include "assistantwidget.h"

#include <QApplication>
#include <QPalette>
#include <QTabWidget>
#include <QVBoxLayout>

#include "aiadvisorwidget.h"
#include "aisimulatorwidget.h"
#include "offlineadvisorwidget.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
char const AI_TAB_PAGE_OBJECT_NAME[]{"aiTabPage"};

void prepareAiTabPage(QWidget *widget) {
  widget->setObjectName(AI_TAB_PAGE_OBJECT_NAME);
}
} // namespace

AiWidget::AiWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_tabWidget = new QTabWidget(this);
  m_tabWidget->setStyleSheet(theme::flatTabStyleSheet());
  mainLay->addWidget(m_tabWidget);

  auto *simulatorWidget = new AiSimulatorWidget(resHolder, this);
  prepareAiTabPage(simulatorWidget);
  m_tabWidget->addTab(simulatorWidget, tr("Simulator"));

  auto *advisorWidget = new AiAdvisorWidget(resHolder, this);
  prepareAiTabPage(advisorWidget);
  // the online advisor (PPO, adjusts the gains while the job runs) is hidden for the time being
  int const onlineAdvisorTab{m_tabWidget->addTab(advisorWidget, tr("Online advisor"))};
  m_tabWidget->setTabVisible(onlineAdvisorTab, false);

  auto *offlineAdvisorWidget = new OfflineAdvisorWidget(resHolder, this);
  prepareAiTabPage(offlineAdvisorWidget);
  m_tabWidget->addTab(offlineAdvisorWidget, tr("Advisor"));

  m_assistantWidget = new assistant::AssistantWidget(resHolder, this);
  prepareAiTabPage(m_assistantWidget);
  m_tabWidget->addTab(m_assistantWidget, tr("Assistant"));
}

void AiWidget::setAnalysis(DataAnalysisWidget *analysis, std::function<void()> showPage) {
  m_assistantWidget->setAnalysis(analysis, std::move(showPage));
}

} // namespace deepvaccontrol
