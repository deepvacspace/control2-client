#ifndef AIWIDGET_H
#define AIWIDGET_H

#include <QWidget>

#include "resholder.h"

class QTabWidget;

namespace deepvaccontrol {

// "AI" page. A tabbed container: "Simulator" (default) hosts the GRU
// digital-twin closed-loop simulator (AiSimulatorWidget), "Advisor" runs
// the online PPO advisor against the real chamber (AiAdvisorWidget), and
// "Offline advisor" pre-tunes a fixed PID via CEM search before each real
// run (OfflineAdvisorWidget).
class AiWidget : public QWidget {
  Q_OBJECT
public:
  explicit AiWidget(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  QTabWidget *m_tabWidget;
};
} // namespace deepvaccontrol

#endif // AIWIDGET_H
