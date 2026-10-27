#ifndef PROGRAMPLANRUNNER_H
#define PROGRAMPLANRUNNER_H

#include <QObject>
#include <QTimer>
#include <QVector>

#include "programforecast.h"

namespace deepvaccontrol {
struct ResHolder;

namespace ai {
class ManualPidGuard;

// Runs a program with the AI Advisor's plan: each interval's recommended settings are sent to the chamber (in every
// band) as the interval begins, intervals without a recommendation get the user's own table, and the user's own
// settings come back when the program ends, is stopped or the connection is lost (or on the next start after a crash).
class ProgramPlanRunner : public QObject {
  Q_OBJECT
public:
  explicit ProgramPlanRunner(ResHolder *resHolder, QObject *parent = nullptr);
  ~ProgramPlanRunner() override;

  // Before the job is sent: the user's settings are kept and the first interval's are sent. False (and why) when
  // they can't be.
  bool start(QVector<IntervalPlan> const &plan, QString *error);
  // The job was not sent after all: the user's settings back.
  void abort(QString const &reason);
  [[nodiscard]] bool            isActive() const { return m_active; }
  [[nodiscard]] ManualPidGuard *guard() const { return m_guard; }

private:
  void onStatus();
  void onStates();
  void apply(int interval);
  void finish(QString const &reason);

  ResHolder            *m_resHolder;
  ManualPidGuard       *m_guard;
  QVector<IntervalPlan> m_plan;
  bool                  m_active{false};
  bool                  m_running{false}; // the job was seen running
  int                   m_applied{-1};    // the interval whose settings the chamber has
  QTimer                m_startTimeout;   // the job never started
};

}
}

#endif // PROGRAMPLANRUNNER_H
