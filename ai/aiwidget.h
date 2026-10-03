#ifndef AIWIDGET_H
#define AIWIDGET_H

#include <QWidget>
#include <functional>

#include "resholder.h"

class QTabWidget;

namespace deepvaccontrol {
class DataAnalysisWidget;
namespace assistant {
class AssistantWidget;
}

// "AI" page. A tabbed container: "Simulator" (default) hosts the GRU
// digital-twin closed-loop simulator (AiSimulatorWidget), "Advisor" runs
// the online PPO advisor against the real chamber (AiAdvisorWidget), and
// "Offline advisor" pre-tunes a fixed PID via CEM search before each real
// run (OfflineAdvisorWidget). "Assistant" is the offline, read-only
// natural-language interface to the recorded experiments (assistant::AssistantWidget).
class AiWidget : public QWidget {
  Q_OBJECT
public:
  explicit AiWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  // the assistant's tools read the experiments of the Data analysis page and can show runs there
  void setAnalysis(DataAnalysisWidget *analysis, std::function<void()> showPage);

private:
  QTabWidget                   *m_tabWidget;
  assistant::AssistantWidget   *m_assistantWidget{nullptr};
};
} // namespace deepvaccontrol

#endif // AIWIDGET_H
