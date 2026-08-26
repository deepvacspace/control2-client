#include "advisorcem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>

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

bool CemOptimizer::rolloutCandidate(SimParams const &params, double kp, double ki, double kd,
                                     QVector<TrajectoryPoint> &trajectoryOut) const {
  SimParams candidateParams = params;
  candidateParams.kp        = kp;
  candidateParams.ki        = ki;
  candidateParams.kd        = kd;
  candidateParams.initialP  = 0.0;
  candidateParams.initialI  = 0.0;
  candidateParams.initialD  = 0.0;

  ChamberTwinSim sim(m_model);
  if (!sim.begin(candidateParams)) {
    return false;
  }

  TrajectoryPoint point;
  while (!sim.isFinished()) {
    if (!sim.stepOnce(point)) {
      trajectoryOut = sim.trajectory();
      return false;
    }
  }

  trajectoryOut = sim.trajectory();
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

  double const kpRange = std::max(KP_MAX - KP_MIN, 1e-9);
  double const kiRange = std::max(KI_MAX - KI_MIN, 1e-9);
  double const kdRange = std::max(KD_MAX - KD_MIN, 1e-9);
  double const pidDistance =
    (std::abs(candidateKp - previousKp) / kpRange + std::abs(candidateKi - previousKi) / kiRange
     + std::abs(candidateKd - previousKd) / kdRange)
    / 3.0;

  double cost = CEM_W_OVERSHOOT_MAX * overshootMax + CEM_W_OVERSHOOT_RMSE * overshootRms
              + CEM_W_ABS_ERROR * maeFull + CEM_W_FINAL_ABS_ERROR * finalAbsErr + CEM_W_NEAR_STD * nearStd
              + CEM_W_CONTROL_CHANGE * pidDistance;
  if (!valid) {
    cost += CEM_W_INVALID;
  }

  return cost;
}

CemResult CemOptimizer::run(SimParams const &params, std::function<void(int, int)> const &progressCb) const {
  CemResult result;

  std::array<double, 3> const lo{KP_MIN, KI_MIN, KD_MIN};
  std::array<double, 3> const hi{KP_MAX, KI_MAX, KD_MAX};
  std::array<double, 3> const current{COLD_START_KP, COLD_START_KI, COLD_START_KD};

  std::array<double, 3> mean{current};
  std::array<double, 3> stdev{};
  for (int i{0}; i < 3; ++i) {
    stdev[i] = CEM_INITIAL_STD_FRAC * (hi[i] - lo[i]);
  }

  std::mt19937 rng(42U);

  int const totalCandidates = CEM_POPULATION * CEM_ITERATIONS;
  int       evaluated{0};

  double                   bestCost{std::numeric_limits<double>::infinity()};
  std::array<double, 3>    bestPid{current};
  QVector<TrajectoryPoint> bestTrajectory;

  for (int iter{0}; iter < CEM_ITERATIONS; ++iter) {
    QVector<Candidate> iterCandidates;
    iterCandidates.reserve(CEM_POPULATION);

    for (int sampleIdx{0}; sampleIdx < CEM_POPULATION; ++sampleIdx) {
      std::array<double, 3> sample{};
      if (0 == sampleIdx) {
        sample = current; // always evaluate the cold-start candidate
      } else {
        for (int d{0}; d < 3; ++d) {
          std::normal_distribution<double> dist(mean[d], stdev[d]);
          sample[d] = std::floor(limitValue(lo[d], dist(rng), hi[d]) + 0.5);
        }
      }

      QVector<TrajectoryPoint> trajectory;
      bool const               valid = rolloutCandidate(params, sample[0], sample[1], sample[2], trajectory);
      double const cost = horizonCost(trajectory, params, sample[0], sample[1], sample[2], current[0], current[1],
                                       current[2], valid);

      iterCandidates.push_back({sample[0], sample[1], sample[2], cost});
      if (cost < bestCost) {
        bestCost       = cost;
        bestPid        = sample;
        bestTrajectory = trajectory;
      }

      ++evaluated;
      if (progressCb) {
        progressCb(evaluated, totalCandidates);
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

  return result;
}

} // namespace control2::ai
