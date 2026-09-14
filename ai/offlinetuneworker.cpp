#include "offlinetuneworker.h"

#include <QObject>

#include "advisorcem.h"

namespace control2::ai {

OfflineTuneWorker::OfflineTuneWorker(GruOnnxModel *model, SimParams const &params, QObject *parent)
    : QThread{parent}, m_model{model}, m_params{params} {}

OfflineTuneResult const &OfflineTuneWorker::result() const {
  return m_result;
}

void OfflineTuneWorker::run() {
  m_result = OfflineTuneResult{};

  if (!m_model->isLoaded()) {
    m_result.errorMessage = m_model->statusMessage();
    return;
  }

  CemOptimizer const cem(m_model);
  CemResult const    cemResult = cem.run(m_params, [this](int done, int total) {
    emit progress(done, total);
  });

  if (!cemResult.valid) {
    m_result.errorMessage = QObject::tr("AI Advisor failed");
    return;
  }

  m_result.bestKp         = cemResult.kp;
  m_result.bestKi         = cemResult.ki;
  m_result.bestKd         = cemResult.kd;
  m_result.bestTrajectory = cemResult.trajectory;
  m_result.bestMetrics    = cemResult.metrics;

  if (cemResult.medianValid) {
    m_result.medianKp         = cemResult.medianKp;
    m_result.medianKi         = cemResult.medianKi;
    m_result.medianKd         = cemResult.medianKd;
    m_result.medianTrajectory = cemResult.medianTrajectory;
    m_result.medianMetrics    = cemResult.medianMetrics;
  }

  m_result.ok = true;
}

} // namespace control2::ai
