#include "advisorcem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace control2::ai {

namespace {
double limitValue(double low, double x, double high) {
  return std::max(low, std::min(x, high));
}

struct Candidate {
  double kp;
  double ki;
  double kd;
  double cost;
};
} // namespace

CemOptimizer::CemOptimizer(GruOnnxModel *model) : m_model{model} {}

bool CemOptimizer::rolloutPopulation(SimParams const &params, QVector<std::array<double, 3>> const &candidates,
                                      QVector<QVector<TrajectoryPoint>> &trajectoriesOut, QVector<bool> &validOut,
                                      std::function<void(int, int)> const &stepProgressCb) const {
  int const n = static_cast<int>(candidates.size());
  trajectoriesOut.assign(n, {});
  validOut.assign(n, true);
  if (0 == n) {
    return true;
  }

  QStringList const featureNames = m_model->featureNames();
  int const         windowSteps  = m_model->windowSteps();
  int const         nFeatures    = featureNames.size();
  if ((windowSteps <= 0) || (nFeatures <= 0)) {
    return false;
  }

  double const dtSafe     = std::max(params.dtS, 1e-9);
  double const period     = std::max(PID_PERIOD_S, 1e-6);
  int const    nSubsteps  = std::max(1, static_cast<int>(std::llround(std::max(dtSafe, period) / period)));
  int const    totalSteps = std::max(1, static_cast<int>(std::ceil(params.durationS / dtSafe)));
  double const featureScale = std::max(std::abs(CONTROL_FEATURE_SCALE), 1e-9);

  std::vector<double> kp(n), ki(n), kd(n);
  for (int c{0}; c < n; ++c) {
    kp[c] = candidates[c][0];
    ki[c] = candidates[c][1];
    kd[c] = candidates[c][2];
  }

  std::vector<ChamberPid>  pids(n, ChamberPid(U_MIN, U_MAX, PID_I_REVERSE_MUL));
  std::vector<CodesysDiff> diffs(n);
  for (auto &diff : diffs) {
    diff.prevValue = params.startTemp;
  }

  std::vector<double> currentTemp(n, params.startTemp);
  std::vector<double> previousTemp(n, params.startTemp);
  std::vector<bool>   validSoFar(n, true);

  // Flat batch buffer, row-major [candidate][windowStep][feature], cold-
  // started with each candidate's own placeholder row (features include
  // kp/ki/kd, so this cannot be shared across candidates).
  QVector<float> windowFlat(n * windowSteps * nFeatures);
  for (int c{0}; c < n; ++c) {
    QVector<float> const placeholderRow = buildTwinFeatureRow(featureNames, params.startTemp, params.startTemp,
                                                                params.startTemp, params.dtS, 0.0, 0.0, 0.0, 0.0,
                                                                kp[c], ki[c], kd[c]);
    float             *base = windowFlat.data() + static_cast<qsizetype>(c) * windowSteps * nFeatures;
    for (int t{0}; t < windowSteps; ++t) {
      std::copy(placeholderRow.begin(), placeholderRow.end(), base + static_cast<qsizetype>(t) * nFeatures);
    }
  }

  for (int c{0}; c < n; ++c) {
    trajectoriesOut[c].reserve(totalSteps);
  }

  std::vector<double> u(n), uP(n), uI(n), uD(n);

  for (int step{0}; step < totalSteps; ++step) {
    for (int c{0}; c < n; ++c) {
      for (int k{0}; k < nSubsteps; ++k) {
        double const diffOut = diffs[c].update(currentTemp[c]);
        auto const   terms   = pids[c].step(true, params.targetTemp, currentTemp[c], kp[c], ki[c], kd[c], diffOut);
        u[c]                 = terms[0] * featureScale;
        uP[c]                = terms[1] * featureScale;
        uI[c]                = terms[2] * featureScale;
        uD[c]                = terms[3] * featureScale;
      }

      QVector<float> const lastRow = buildTwinFeatureRow(featureNames, currentTemp[c], params.targetTemp,
                                                           previousTemp[c], params.dtS, u[c], uP[c], uI[c], uD[c],
                                                           kp[c], ki[c], kd[c]);
      float             *base = windowFlat.data() + static_cast<qsizetype>(c) * windowSteps * nFeatures;
      std::copy(lastRow.begin(), lastRow.end(), base + static_cast<qsizetype>(windowSteps - 1) * nFeatures);
    }

    QVector<float> deltaOut;
    if (!m_model->predictDeltaT1Batch(windowFlat, n, deltaOut)) {
      return false;
    }

    if (stepProgressCb) {
      stepProgressCb(step + 1, totalSteps);
    }

    for (int c{0}; c < n; ++c) {
      double const proposedTemp = currentTemp[c] + static_cast<double>(deltaOut[c]);
      bool const   stepValid    = std::isfinite(proposedTemp) && (std::abs(proposedTemp) <= MAX_ABS_TEMP);
      double const nextTemp =
        stepValid ? proposedTemp
                  : limitValue(-MAX_ABS_TEMP, std::isfinite(proposedTemp) ? proposedTemp : MAX_ABS_TEMP, MAX_ABS_TEMP);

      if (validSoFar[c]) {
        TrajectoryPoint point;
        point.elapsedS = (step + 1) * params.dtS;
        point.temp     = nextTemp;
        point.tempRef  = params.targetTemp;
        point.error    = params.targetTemp - nextTemp;
        point.u        = u[c];
        point.uP       = uP[c];
        point.uI       = uI[c];
        point.uD       = uD[c];
        trajectoriesOut[c].push_back(point);

        if (!stepValid) {
          validSoFar[c] = false;
          validOut[c]   = false;
        }
      }

      QVector<float> const nextRow = buildTwinFeatureRow(featureNames, nextTemp, params.targetTemp, currentTemp[c],
                                                           params.dtS, u[c], uP[c], uI[c], uD[c], kp[c], ki[c], kd[c]);
      float             *base = windowFlat.data() + static_cast<qsizetype>(c) * windowSteps * nFeatures;
      std::memmove(base, base + nFeatures, static_cast<size_t>(windowSteps - 1) * nFeatures * sizeof(float));
      std::copy(nextRow.begin(), nextRow.end(), base + static_cast<qsizetype>(windowSteps - 1) * nFeatures);

      previousTemp[c] = currentTemp[c];
      currentTemp[c]  = nextTemp;
    }
  }

  return true;
}

double CemOptimizer::horizonCost(QVector<TrajectoryPoint> const &trajectory, SimParams const &params,
                                  double candidateKp, double candidateKi, double candidateKd, double previousKp,
                                  double previousKi, double previousKd, bool valid) const {
  if (trajectory.isEmpty()) {
    return CEM_W_INVALID;
  }

  double sumAbsError{0.0};
  double overshootMax{0.0};
  double sumOvershootSq{0.0};
  double nearSum{0.0};
  int    nearCount{0};

  for (auto const &p : trajectory) {
    double const absErr = std::abs(p.error);
    sumAbsError += absErr;

    double overshoot{0.0};
    if (params.targetTemp <= params.startTemp) {
      overshoot = std::max(params.targetTemp - p.temp, 0.0);
    } else {
      overshoot = std::max(p.temp - params.targetTemp, 0.0);
    }
    overshootMax = std::max(overshootMax, overshoot);
    sumOvershootSq += overshoot * overshoot;

    if (absErr <= NEAR_BAND) {
      nearSum += p.temp;
      ++nearCount;
    }
  }

  int const    n            = trajectory.size();
  double const maeFull      = sumAbsError / n;
  double const overshootRms = std::sqrt(sumOvershootSq / n);
  double const finalAbsErr  = std::abs(trajectory.last().error);

  double nearStd{0.0};
  if (nearCount > 0) {
    double const nearMean = nearSum / nearCount;
    double       varSum{0.0};
    for (auto const &p : trajectory) {
      if (std::abs(p.error) <= NEAR_BAND) {
        double const d = p.temp - nearMean;
        varSum += d * d;
      }
    }
    nearStd = std::sqrt(varSum / nearCount);
  }

  // Std-dev over just the last TAIL_WINDOW_S of the horizon: genuine
  // post-settle stability, independent of how the candidate got there.
  double const dtSafe     = std::max(params.dtS, 1e-9);
  int const    nTailSteps = std::max(1, static_cast<int>(std::llround(TAIL_WINDOW_S / dtSafe)));
  int const    tailStart  = std::max(0, n - nTailSteps);
  int const    tailCount  = n - tailStart;

  double tailStd{0.0};
  if (tailCount > 1) {
    double tailSum{0.0};
    for (int i{tailStart}; i < n; ++i) {
      tailSum += trajectory[i].temp;
    }
    double const tailMean = tailSum / tailCount;
    double       tailVarSum{0.0};
    for (int i{tailStart}; i < n; ++i) {
      double const d = trajectory[i].temp - tailMean;
      tailVarSum += d * d;
    }
    tailStd = std::sqrt(tailVarSum / tailCount);
  }

  double const kpRange = std::max(KP_MAX - KP_MIN, 1e-9);
  double const kiRange = std::max(KI_MAX - KI_MIN, 1e-9);
  double const kdRange = std::max(KD_MAX - KD_MIN, 1e-9);
  double const zKp      = (candidateKp - previousKp) / kpRange;
  double const zKi      = (candidateKi - previousKi) / kiRange;
  double const zKd      = (candidateKd - previousKd) / kdRange;
  double const pidDistance = std::sqrt((zKp * zKp + zKi * zKi + zKd * zKd) / 3.0);

  double cost = CEM_W_OVERSHOOT_MAX * overshootMax + CEM_W_OVERSHOOT_RMSE * overshootRms
              + CEM_W_ABS_ERROR * maeFull + CEM_W_FINAL_ABS_ERROR * finalAbsErr + CEM_W_NEAR_STD * nearStd
              + CEM_W_TAIL_STD * tailStd + CEM_W_CONTROL_CHANGE * pidDistance;
  if (!valid) {
    cost += CEM_W_INVALID;
  }

  return cost;
}

CemResult CemOptimizer::run(SimParams const &params, std::function<void(int, int)> const &progressCb) const {
  CemResult result;

  if (!std::isfinite(params.startTemp) || !std::isfinite(params.targetTemp) || !std::isfinite(params.durationS)
      || !std::isfinite(params.dtS)) {
    return result; // valid stays false: garbage in, no point searching
  }

  std::array<double, 3> const lo{KP_MIN, KI_MIN, KD_MIN};
  std::array<double, 3> const hi{KP_MAX, KI_MAX, KD_MAX};
  std::array<double, 3> const current{COLD_START_KP, COLD_START_KI, COLD_START_KD};

  std::array<double, 3> mean{};
  std::array<double, 3> stdev{};

  std::mt19937 rng(42U);

  int const totalSteps           = std::max(1, static_cast<int>(std::ceil(params.durationS / std::max(params.dtS, 1e-9))));
  int const totalStepEvaluations = totalSteps * CEM_ITERATIONS;
  int       stepsDone{0};

  double                   bestCost{std::numeric_limits<double>::infinity()};
  bool                     foundAny{false};
  std::array<double, 3>    bestPid{current};
  QVector<TrajectoryPoint> bestTrajectory;
  double                   currentCost{CEM_W_INVALID}; // cold-start triplet's own cost, sample[0] of iter 0
  QVector<TrajectoryPoint> currentTrajectory;
  QVector<Candidate>       allCandidates;
  allCandidates.reserve(CEM_POPULATION * CEM_ITERATIONS);

  for (int iter{0}; iter < CEM_ITERATIONS; ++iter) {
    QVector<std::array<double, 3>> samples(CEM_POPULATION);
    for (int sampleIdx{0}; sampleIdx < CEM_POPULATION; ++sampleIdx) {
      for (int d{0}; d < 3; ++d) {
        double raw;
        if (0 == iter) {
          // First iteration samples uniformly across the whole PID box so
          // the search covers the full range: bounded by construction, so
          // there's no out-of-bounds mass to clip -- a wide Gaussian here
          // instead pushes much of its tail past the bounds in each of the
          // 3 dimensions independently, and clipping pins every one of
          // those draws onto the exact boundary, biasing the very first
          // elite population toward box corners rather than the interior.
          std::uniform_real_distribution<double> dist(lo[d], hi[d]);
          raw = dist(rng);
        } else {
          std::normal_distribution<double> dist(mean[d], stdev[d]);
          raw = dist(rng);
        }
        samples[sampleIdx][d] = limitValue(lo[d], std::floor(limitValue(lo[d], raw, hi[d]) + 0.5), hi[d]);
      }
    }
    samples[0] = current; // always evaluate the cold-start triplet as one candidate

    QVector<QVector<TrajectoryPoint>> trajectories;
    QVector<bool>                     valids;
    bool const rolloutOk = rolloutPopulation(params, samples, trajectories, valids, [&](int doneSteps, int) {
      if (progressCb) {
        progressCb(stepsDone + doneSteps, totalStepEvaluations);
      }
    });
    stepsDone += totalSteps;

    QVector<Candidate> iterCandidates;
    iterCandidates.reserve(CEM_POPULATION);
    for (int sampleIdx{0}; sampleIdx < CEM_POPULATION; ++sampleIdx) {
      std::array<double, 3> const &sample = samples[sampleIdx];
      double const                 cost   = rolloutOk ? horizonCost(trajectories[sampleIdx], params, sample[0],
                                                                     sample[1], sample[2], current[0], current[1],
                                                                     current[2], valids[sampleIdx])
                                                        : CEM_W_INVALID;

      iterCandidates.push_back({sample[0], sample[1], sample[2], cost});
      allCandidates.push_back({sample[0], sample[1], sample[2], cost});
      if (cost < bestCost) {
        bestCost       = cost;
        bestPid        = sample;
        bestTrajectory = rolloutOk ? trajectories[sampleIdx] : QVector<TrajectoryPoint>{};
        foundAny       = true;
      }
      if ((0 == iter) && (0 == sampleIdx)) {
        currentCost       = cost;
        currentTrajectory = rolloutOk ? trajectories[sampleIdx] : QVector<TrajectoryPoint>{};
      }
    }

    std::sort(iterCandidates.begin(), iterCandidates.end(), [](Candidate const &a, Candidate const &b) {
      return a.cost < b.cost;
    });

    int const eliteN = std::max(2, static_cast<int>(std::round(CEM_POPULATION * CEM_ELITE_FRAC)));
    int const nElite = std::min(eliteN, static_cast<int>(iterCandidates.size()));

    for (int d{0}; d < 3; ++d) {
      double sum{0.0};
      for (int i{0}; i < nElite; ++i) {
        double const v = (0 == d) ? iterCandidates[i].kp : (1 == d ? iterCandidates[i].ki : iterCandidates[i].kd);
        sum += v;
      }
      double const eliteMean = sum / nElite;

      double varSum{0.0};
      for (int i{0}; i < nElite; ++i) {
        double const v = (0 == d) ? iterCandidates[i].kp : (1 == d ? iterCandidates[i].ki : iterCandidates[i].kd);
        double const diff = v - eliteMean;
        varSum += diff * diff;
      }
      double const eliteStd = std::sqrt(varSum / nElite);

      mean[d]  = eliteMean;
      stdev[d] = std::max(eliteStd, CEM_MIN_STD_FRAC * (hi[d] - lo[d]));
    }
  }

  if (!foundAny) {
    return result; // every candidate came back non-comparable (e.g. NaN cost) -- report failure, not the untested default
  }

  // Safety gate: only recommend a change if it actually beats the
  // cold-start triplet's own rolled-out cost -- otherwise keep it.
  if (bestCost >= currentCost) {
    bestCost       = currentCost;
    bestPid        = current;
    bestTrajectory = currentTrajectory;
  }

  result.valid      = true;
  result.kp         = bestPid[0];
  result.ki         = bestPid[1];
  result.kd         = bestPid[2];
  result.cost       = bestCost;
  result.trajectory = bestTrajectory;

  // Simulator-style metrics (same formula as the Simulator tab) for a fair,
  // consistent comparison against the advisor's Actual/Expected runs.
  SimParams finalParams = params;
  finalParams.kp         = bestPid[0];
  finalParams.ki         = bestPid[1];
  finalParams.kd         = bestPid[2];
  finalParams.initialP   = 0.0;
  finalParams.initialI   = 0.0;
  finalParams.initialD   = 0.0;

  ChamberTwinSim sim(m_model);
  if (sim.begin(finalParams)) {
    TrajectoryPoint point;
    while (!sim.isFinished()) {
      if (!sim.stepOnce(point)) {
        break;
      }
    }
    result.metrics = sim.metrics();
  }

  // Median-cost candidate across the whole population: a real,
  // already-evaluated triplet representative of a typical (not optimized)
  // fixed-PID choice.
  if (!allCandidates.isEmpty()) {
    std::sort(allCandidates.begin(), allCandidates.end(), [](Candidate const &a, Candidate const &b) {
      return a.cost < b.cost;
    });
    Candidate const &median = allCandidates[allCandidates.size() / 2];

    SimParams medianParams = params;
    medianParams.kp         = median.kp;
    medianParams.ki         = median.ki;
    medianParams.kd         = median.kd;
    medianParams.initialP   = 0.0;
    medianParams.initialI   = 0.0;
    medianParams.initialD   = 0.0;

    ChamberTwinSim medianSim(m_model);
    if (medianSim.begin(medianParams)) {
      TrajectoryPoint point;
      while (!medianSim.isFinished()) {
        if (!medianSim.stepOnce(point)) {
          break;
        }
      }
      result.medianValid      = true;
      result.medianKp         = median.kp;
      result.medianKi         = median.ki;
      result.medianKd         = median.kd;
      result.medianTrajectory = medianSim.trajectory();
      result.medianMetrics    = medianSim.metrics();
    }
  }

  return result;
}

} // namespace control2::ai
