#ifndef AIWIDGET_H
#define AIWIDGET_H

#include <QWidget>

#include "resholder.h"

class QTabWidget;

namespace control2 {

// "AI" page. A tabbed container, matching the tabbing pattern used
// elsewhere in the app (see SystemWidget): "Simulator" (default) hosts the
// GRU digital-twin closed-loop simulator (AiSimulatorWidget), "Advisor" is
// a placeholder for the future PID advisor (AiAdvisorWidget).
class AiWidget : public QWidget {
  Q_OBJECT
public:
  explicit AiWidget(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  QTabWidget *m_tabWidget;
};
} // namespace control2

#endif // AIWIDGET_H
