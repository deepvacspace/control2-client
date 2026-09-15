#include "chambertwinsim.h"

#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>

namespace deepvaccontrol::ai {

namespace {
double limitValue(double low, double x, double high) {
  return std::max(low, std::min(x, high));
}
} // namespace

QVector<float> buildTwinFeatureRow(QStringList const &featureNames, double temp, double tempRef,
                                    double previousTemp, double dtS, double u, double uP, double uI, double uD,
                                    double kp, double ki, double kd) {
  double const error  = tempRef - temp;
  double const dtSafe = std::max(dtS, 1e-9);

  QVector<float> row(featureNames.size(), 0.0f);
  for (int i{0}; i < featureNames.size(); ++i) {
    QString const &name = featureNames.at(i);
    double          value{0.0};
    if ("temp" == name) {
      value = temp;
    } else if ("temp_ref" == name) {
      value = tempRef;
    } else if ("error" == name) {
      value = error;
    } else if ("temp_u" == name) {
      value = u;
    } else if ("temp_u_p" == name) {
      value = uP;
    } else if ("temp_u_i" == name) {
      value = uI;
    } else if ("temp_u_d" == name) {
      value = uD;
    } else if ("kp" == name) {
      value = kp;
    } else if ("ki" == name) {
      value = ki;
    } else if ("kd" == name) {
      value = kd;
    } else if ("abs_error" == name) {
      value = std::abs(error);
    } else if ("dt_s" == name) {
      value = dtS;
    } else if ("temp_velocity" == name) {
      value = (temp - previousTemp) / dtSafe;
    } else if ("error_velocity" == name) {
      value = -(temp - previousTemp) / dtSafe;
    }
    row[i] = static_cast<float>(value);
  }
  return row;
}

double CodesysDiff::update(double value) {
  double const diffValue = value - prevValue;
  double const filterIn  = limitValue(-5.0, diffValue, 5.0);
  filterOut               = m_dc * filterOut + (1.0 - m_dc) * filterIn;
  prevValue               = value;
  out                     = 10.0 * limitValue(-5.0, filterOut, 5.0);
  return out;
}

ChamberPid::ChamberPid(double uMin, double uMax, double iReverseMul)
    : m_uMin{uMin}, m_uMax{uMax}, m_iReverseMul{iReverseMul} {}

std::array<double, 4> ChamberPid::step(
  bool enable, double xTarget, double xMeasured, double pCoef, double iCoef, double dCoef, double diffOut) {
  if (!enable) {
    pPart = 0.0;
    iPart = 0.0;
    dPart = 0.0;
    return {0.0, pPart, iPart, dPart};
  }

  double const delta = xTarget - xMeasured;

  if (0.0 == pCoef) {
    return {0.0, pPart, iPart, dPart};
  }

  pPart = (1.0 / pCoef) * delta;

  double effectiveICoef = iCoef;
  if (delta * iPart < 0.0) {
    effectiveICoef = iCoef * m_iReverseMul;
  }

  double const deltaEdge = 1.2 * pCoef;

  if (0.0 != effectiveICoef && std::abs(delta) < deltaEdge) {
    iPart += (1.0 / pCoef) * (delta * 0.1 / effectiveICoef);
  }

  dPart = (1.0 / pCoef) * (dCoef * -diffOut);

  iPart = limitValue(m_uMin, iPart, m_uMax);
  dPart = limitValue(-0.4, dPart, 0.4);

  double u = pPart + iPart + dPart;
  u        = limitValue(m_uMin, u, m_uMax);

  // Clipped for logging only, matches the Python reference.
  pPart = limitValue(m_uMin, pPart, m_uMax);

  return {u, pPart, iPart, dPart};
}

ChamberTwinSim::ChamberTwinSim(GruOnnxModel *model) : m_model{model} {}

bool ChamberTwinSim::begin(SimParams const &params, QString *errorOut) {
  if (nullptr == m_model || !m_model->isLoaded()) {
    if (errorOut) {
      *errorOut = m_model ? m_model->statusMessage() : QObject::tr("Model not set");
    }
    return false;
  }

  m_params       = params;
  m_featureNames = m_model->featureNames();
  m_windowSteps  = m_model->windowSteps();
  m_nFeatures    = m_featureNames.size();

  m_pid       = ChamberPid(U_MIN, U_MAX, PID_I_REVERSE_MUL);
  m_pid.pPart = params.initialP;
  m_pid.iPart = params.initialI;
  m_pid.dPart = params.initialD;

  m_diff           = CodesysDiff();
  m_diff.prevValue = params.startTemp;
  m_diff.filterOut = 0.0;
  m_diff.out       = 0.0;

  // Flat placeholder window at start_temp with zero control terms, matching
  // insight's initialize_feature_window() (precondition_ref == start_temp).
  m_featureWindow.clear();
  m_featureWindow.reserve(m_windowSteps * m_nFeatures);
  for (int i{0}; i < m_windowSteps; ++i) {
    m_featureWindow += buildTwinFeatureRow(m_featureNames, params.startTemp, params.startTemp, params.startTemp,
                                            params.dtS, 0.0, 0.0, 0.0, 0.0, params.kp, params.ki, params.kd);
  }

  m_currentTemp   = params.startTemp;
  m_previousTemp  = params.startTemp;
  m_step          = 0;
  m_totalSteps    = std::max(1, static_cast<int>(std::ceil(params.durationS / std::max(params.dtS, 1e-9))));
  m_valid         = true;
  m_invalidReason.clear();
  m_trajectory.clear();
  m_trajectory.reserve(m_totalSteps);

  return true;
}

bool ChamberTwinSim::stepOnce(TrajectoryPoint &pointOut, QString *errorOut) {
  if (isFinished()) {
    return false;
  }

  double const featureScale = std::max(std::abs(CONTROL_FEATURE_SCALE), 1e-9);

  // run_pid_substeps(): fine-grained PID/Diff ticks within this dt_s step
  // (the PLC scans much faster than the model's sampling interval).
  double const period    = std::max(PID_PERIOD_S, 1e-6);
  double const dt        = std::max(m_params.dtS, period);
  int const    nSubsteps = std::max(1, static_cast<int>(std::llround(dt / period)));

  double u{0.0}, uP{0.0}, uI{0.0}, uD{0.0};
  for (int k{0}; k < nSubsteps; ++k) {
    double const diffOut = m_diff.update(m_currentTemp); // temp_mode == "hold"
    auto const terms =
      m_pid.step(true, m_params.targetTemp, m_currentTemp, m_params.kp, m_params.ki, m_params.kd, diffOut);
    u  = terms[0] * featureScale;
    uP = terms[1] * featureScale;
    uI = terms[2] * featureScale;
    uD = terms[3] * featureScale;
  }

  // predict_next(): patch the last window row with this step's live terms,
  // then run the plant model on the patched window.
  QVector<float>      predictWindow = m_featureWindow;
  QVector<float> const lastRow      = buildTwinFeatureRow(m_featureNames, m_currentTemp, m_params.targetTemp,
                                                            m_previousTemp, m_params.dtS, u, uP, uI, uD, m_params.kp,
                                                            m_params.ki, m_params.kd);
  std::copy(lastRow.begin(), lastRow.end(), predictWindow.end() - m_nFeatures);

  float predDelta{0.0f};
  if (!m_model->predictDeltaT1(predictWindow, predDelta)) {
    if (errorOut) {
      *errorOut = m_model->statusMessage();
    }
    return false;
  }

  double nextTemp = m_currentTemp + static_cast<double>(predDelta);
  bool const stepValid = std::isfinite(nextTemp) && (std::abs(nextTemp) <= MAX_ABS_TEMP);
  if (!stepValid) {
    if (m_valid) {
      m_valid         = false;
      m_invalidReason = QObject::tr("Temperature went out of valid range at step %1: %2").arg(m_step).arg(nextTemp);
    }
    if (!std::isfinite(nextTemp)) {
      nextTemp = MAX_ABS_TEMP;
    } else {
      nextTemp = limitValue(-MAX_ABS_TEMP, nextTemp, MAX_ABS_TEMP);
    }
  }

  // Roll the window forward: drop the oldest row, append the realized
  // next-step row (matches np.roll(pred_window, -1) then overwriting [-1]).
  QVector<float> const nextRow =
    buildTwinFeatureRow(m_featureNames, nextTemp, m_params.targetTemp, m_currentTemp, m_params.dtS, u, uP, uI, uD,
                         m_params.kp, m_params.ki, m_params.kd);
  m_featureWindow = predictWindow.mid(m_nFeatures) + nextRow;

  pointOut.elapsedS  = (m_step + 1) * m_params.dtS;
  pointOut.temp      = nextTemp;
  pointOut.tempRef   = m_params.targetTemp;
  pointOut.error     = m_params.targetTemp - nextTemp;
  pointOut.u         = u;
  pointOut.uP        = uP;
  pointOut.uI        = uI;
  pointOut.uD        = uD;
  pointOut.predDelta = predDelta;

  m_previousTemp = m_currentTemp;
  m_currentTemp  = nextTemp;
  ++m_step;
  m_trajectory.push_back(pointOut);

  return true;
}

bool ChamberTwinSim::isFinished() const {
  return m_step >= m_totalSteps;
}

int ChamberTwinSim::stepsDone() const {
  return m_step;
}

int ChamberTwinSim::totalSteps() const {
  return m_totalSteps;
}

void ChamberTwinSim::setPid(double kp, double ki, double kd) {
  m_params.kp = kp;
  m_params.ki = ki;
  m_params.kd = kd;
}

double ChamberTwinSim::currentTemp() const {
  return m_currentTemp;
}

double ChamberTwinSim::previousTemp() const {
  return m_previousTemp;
}

double ChamberTwinSim::kp() const {
  return m_params.kp;
}

double ChamberTwinSim::ki() const {
  return m_params.ki;
}

double ChamberTwinSim::kd() const {
  return m_params.kd;
}

double ChamberTwinSim::targetTemp() const {
  return m_params.targetTemp;
}

double ChamberTwinSim::pidIntegral() const {
  return m_pid.iPart;
}

double ChamberTwinSim::diffFilterOut() const {
  return m_diff.filterOut;
}

double ChamberTwinSim::elapsedS() const {
  return m_step * m_params.dtS;
}

QVector<TrajectoryPoint> const &ChamberTwinSim::trajectory() const {
  return m_trajectory;
}

SimMetrics ChamberTwinSim::metrics() const {
  SimMetrics metrics;
  metrics.valid         = m_valid;
  metrics.invalidReason = m_invalidReason;

  if (m_trajectory.isEmpty()) {
    return metrics;
  }

  double const tailStart = std::max(0.0, m_params.durationS - TAIL_WINDOW_S);

  bool anyTail{false};
  for (auto const &p : m_trajectory) {
    if (p.elapsedS >= tailStart) {
      anyTail = true;
      break;
    }
  }

  double sumAbsErrorFull{0.0};
  double sumTailAbsError{0.0};
  double sumTailError{0.0};
  double tailTempSum{0.0};
  int    tailCount{0};
  double minTemp{std::numeric_limits<double>::infinity()};
  double maxTemp{-std::numeric_limits<double>::infinity()};
  double overshootMax{0.0};
  double sumOvershootSq{0.0};
  double timeToNear{m_params.durationS + 999.0};
  double timeToSettle{m_params.durationS + 999.0};
  bool   foundNear{false};
  bool   foundSettle{false};

  for (auto const &p : m_trajectory) {
    double const absErr = std::abs(p.error);
    sumAbsErrorFull += absErr;
    minTemp = std::min(minTemp, p.temp);
    maxTemp = std::max(maxTemp, p.temp);

    double overshoot{0.0};
    if (m_params.targetTemp <= m_params.startTemp) {
      overshoot = std::max(m_params.targetTemp - p.temp, 0.0); // cooling: below target
    } else {
      overshoot = std::max(p.temp - m_params.targetTemp, 0.0); // heating: above target
    }
    overshootMax = std::max(overshootMax, overshoot);
    sumOvershootSq += overshoot * overshoot;

    bool const inTail = anyTail ? (p.elapsedS >= tailStart) : true;
    if (inTail) {
      sumTailAbsError += absErr;
      sumTailError += p.error;
      tailTempSum += p.temp;
      ++tailCount;
    }

    if (!foundNear && absErr <= NEAR_BAND) {
      timeToNear = p.elapsedS;
      foundNear  = true;
    }
    if (!foundSettle && absErr <= SETTLE_BAND) {
      timeToSettle = p.elapsedS;
      foundSettle  = true;
    }
  }

  int const    n        = m_trajectory.size();
  double const tailMean = tailCount > 0 ? tailTempSum / tailCount : 0.0;
  double       sumTailVar{0.0};
  if (tailCount > 0) {
    for (auto const &p : m_trajectory) {
      bool const inTail = anyTail ? (p.elapsedS >= tailStart) : true;
      if (inTail) {
        double const d = p.temp - tailMean;
        sumTailVar += d * d;
      }
    }
  }

  metrics.maeFull       = sumAbsErrorFull / n;
  metrics.tailMae       = tailCount > 0 ? sumTailAbsError / tailCount : 0.0;
  metrics.tailBias      = tailCount > 0 ? sumTailError / tailCount : 0.0;
  metrics.tailStd       = tailCount > 0 ? std::sqrt(sumTailVar / tailCount) : 0.0;
  metrics.overshootMax  = overshootMax;
  metrics.overshootRmse = std::sqrt(sumOvershootSq / n);
  metrics.finalError    = m_trajectory.last().error;
  metrics.finalAbsError = std::abs(metrics.finalError);
  metrics.timeToNearS   = timeToNear;
  metrics.timeToSettleS = timeToSettle;
  metrics.minTemp       = minTemp;
  metrics.maxTemp       = maxTemp;
  metrics.endTemp       = m_trajectory.last().temp;

  metrics.cost = W_TAIL_MAE * metrics.tailMae + W_OVERSHOOT_MAX * metrics.overshootMax
               + W_TAIL_STD * metrics.tailStd + W_FINAL_ERROR * metrics.finalAbsError;
  if (!metrics.valid) {
    metrics.cost += W_INVALID;
  }

  return metrics;
}

} // namespace deepvaccontrol::ai
