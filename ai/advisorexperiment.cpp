#include "advisorexperiment.h"

#include <QObject>

namespace control2::ai {

ComparisonWorker::ComparisonWorker(GruOnnxModel *model, SimParams const &params,
                                    QVector<AdvisorDecision> const &decisions, QObject *parent)
    : QThread{parent}, m_model{model}, m_params{params}, m_decisions{decisions} {}

ComparisonResult const &ComparisonWorker::result() const {
  return m_result;
}

void ComparisonWorker::run() {
  m_result = ComparisonResult{};

  if (!m_model->isLoaded()) {
    m_result.errorMessage = m_model->statusMessage();
    return;
  }
  if (m_decisions.isEmpty()) {
    m_result.errorMessage = QObject::tr("No recorded decisions to replay");
    return;
  }

  // --- Expected: replay the exact recorded decision log ---
  {
    SimParams expParams = m_params;
    expParams.kp         = m_decisions.first().kp;
    expParams.ki         = m_decisions.first().ki;
    expParams.kd         = m_decisions.first().kd;
    expParams.initialP   = 0.0;
    expParams.initialI   = 0.0;
    expParams.initialD   = 0.0;

    ChamberTwinSim sim(m_model);
    if (sim.begin(expParams, &m_result.errorMessage)) {
      int             nextDecisionIdx{1};
      TrajectoryPoint point;
      while (!sim.isFinished()) {
        if (!sim.stepOnce(point, &m_result.errorMessage)) {
          break;
        }
        while ((nextDecisionIdx < m_decisions.size())
               && (point.elapsedS + 1e-6 >= m_decisions[nextDecisionIdx].elapsedS)) {
          AdvisorDecision const &decision = m_decisions[nextDecisionIdx];
          sim.setPid(decision.kp, decision.ki, decision.kd);
          ++nextDecisionIdx;
        }
      }
      m_result.expectedTrajectory = sim.trajectory();
      m_result.expectedMetrics    = sim.metrics();
    }
  }

  // --- CEM baseline: one full-duration search for a single best triplet ---
  {
    CemOptimizer const cem(m_model);
    CemResult const    cemResult = cem.run(m_params, [this](int done, int total) {
      emit progress(done, total);
    });
    m_result.cemTrajectory = cemResult.trajectory;
    m_result.cemMetrics    = cemResult.metrics;
    m_result.cemKp         = cemResult.kp;
    m_result.cemKi         = cemResult.ki;
    m_result.cemKd         = cemResult.kd;
  }

  m_result.ok = true;
}

} // namespace control2::ai
