#ifndef AIWIDGET_H
#define AIWIDGET_H

#include <QWidget>
#include <functional>

#include "resholder.h"

class QTabWidget;

namespace deepvaccontrol {
class AnalysisServices;
namespace assistant {
class AssistantWidget;
}

// "AI" page. A tabbed container: "Simulator" (OfflineAdvisorWidget) pre-tunes a fixed PID via CEM search before
// a real run and simulates any P / I / D with the digital twin; the online PPO advisor (AiAdvisorWidget) is hidden.
// "Assistant" is the offline, read-only natural-language interface to the experiments (assistant::AssistantWidget).
class AiWidget : public QWidget {
  Q_OBJECT
public:
  explicit AiWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  // the assistant's tools read the experiments of the Data analysis page and can show runs there
  void setServices(AnalysisServices *services, std::function<void()> showPage);
  // the assistant tab, about this experiment, with this question asked (if any)
  void openAssistant(QString const &run, QString const &question);
  [[nodiscard]] assistant::AssistantWidget *assistantWidget() const;

private:
  QTabWidget                   *m_tabWidget;
  assistant::AssistantWidget   *m_assistantWidget{nullptr};
};
} // namespace deepvaccontrol

#endif // AIWIDGET_H
