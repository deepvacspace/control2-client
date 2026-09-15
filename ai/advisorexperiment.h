#ifndef ADVISOREXPERIMENT_H
#define ADVISOREXPERIMENT_H

#include <QString>
#include <QThread>
#include <QVector>

#include "advisorcem.h"
#include "chambertwinsim.h"
#include "gruonnxmodel.h"

namespace deepvaccontrol::ai {

// One PID decision the advisor made during the live "Actual" run, and the
// sim time (elapsedS) at which it took effect. The whole log is recorded
// during that run so it can be replayed against the twin afterwards, only
// if the user asks to see the comparison.
struct AdvisorDecision {
  double elapsedS{0.0};
  double kp{0.0};
  double ki{0.0};
  double kd{0.0};
};

struct ComparisonResult {
  bool    ok{false};
  QString errorMessage;

  // "Expected": replays the exact recorded decision log (every decision,
  // at its original timestamp) against a fresh instance of the digital
  // twin, from the same starting conditions as the actual run -- i.e.
  // "what the twin says this exact decision sequence should have produced".
  QVector<TrajectoryPoint> expectedTrajectory;
  SimMetrics               expectedMetrics;

  // "CEM baseline": one full-duration CEM search for a single best-fixed
  // PID triplet (foresight's advise_pid.py --mode suggest) -- "what an
  // average/near-optimal fixed-PID run would have looked like".
  QVector<TrajectoryPoint> cemTrajectory;
  SimMetrics               cemMetrics;
  double                   cemKp{0.0};
  double                   cemKi{0.0};
  double                   cemKd{0.0};
};

// Computes the Expected-replay and CEM-baseline curves on a background
// thread (the CEM search alone evaluates dozens of full-duration
// rollouts) -- only started once the user has explicitly asked to see the
// comparison, since it can take a while.
class ComparisonWorker : public QThread {
  Q_OBJECT
public:
  ComparisonWorker(GruOnnxModel *model, SimParams const &params, QVector<AdvisorDecision> const &decisions,
                    QObject *parent = nullptr);

  ComparisonResult const &result() const;

signals:
  void progress(int done, int total);

protected:
  void run() override;

private:
  GruOnnxModel            *m_model;
  SimParams                m_params;
  QVector<AdvisorDecision> m_decisions;
  ComparisonResult         m_result;
};

} // namespace deepvaccontrol::ai

#endif // ADVISOREXPERIMENT_H
