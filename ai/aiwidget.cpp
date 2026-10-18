#include "aiwidget.h"

#include "assistantwidget.h"

#include <QApplication>
#include <QPalette>
#include <QTabWidget>
#include <QVBoxLayout>

#include "aiadvisorwidget.h"
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
  theme::setFlatTabs(m_tabWidget);
  mainLay->addWidget(m_tabWidget);

  auto *advisorWidget = new AiAdvisorWidget(resHolder, this);
  prepareAiTabPage(advisorWidget);
  // the online advisor (PPO, adjusts the gains while the job runs) is hidden for the time being
  int const onlineAdvisorTab{m_tabWidget->addTab(advisorWidget, tr("Online advisor"))};
  m_tabWidget->setTabVisible(onlineAdvisorTab, false);

  auto *offlineAdvisorWidget = new OfflineAdvisorWidget(resHolder, this);
  prepareAiTabPage(offlineAdvisorWidget);
  // the AI Advisor and the digital-twin simulation in one tab
  m_tabWidget->addTab(offlineAdvisorWidget, tr("Simulator"));

  m_assistantWidget = new assistant::AssistantWidget(resHolder, this);
  prepareAiTabPage(m_assistantWidget);
  m_tabWidget->addTab(m_assistantWidget, tr("Assistant"));
  m_tabWidget->setCurrentWidget(offlineAdvisorWidget); // not the hidden online advisor (first in the tab order)
}

assistant::AssistantWidget *AiWidget::assistantWidget() const {
  return m_assistantWidget;
}

void AiWidget::openAssistant(QString const &run, QString const &question) {
  m_tabWidget->setCurrentWidget(m_assistantWidget);
  m_assistantWidget->askAbout(run, question);
}

void AiWidget::setServices(AnalysisServices *services, std::function<void()> showPage) {
  m_assistantWidget->setServices(services, std::move(showPage));
}

} // namespace deepvaccontrol
