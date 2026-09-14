#ifndef ADVISORCEM_H
#define ADVISORCEM_H

#include <QString>
#include <QVector>
#include <array>
#include <functional>

#include "chambertwinsim.h"
#include "gruonnxmodel.h"

namespace control2::ai {

// Cost weights mirrored from foresight's advisor/advise_pid.py::add_cost_args
// (horizon_cost(), used by --mode suggest) -- distinct from the Simulator
// tab's own cost weights.
constexpr double CEM_W_OVERSHOOT_MAX{3.0};
constexpr double CEM_W_OVERSHOOT_RMSE{2.0};
constexpr double CEM_W_ABS_ERROR{0.2};
constexpr double CEM_W_FINAL_ABS_ERROR{1.0};
constexpr double CEM_W_NEAR_STD{0.0};
constexpr double CEM_W_TAIL_STD{300.0};
constexpr double CEM_W_CONTROL_CHANGE{0.01};
constexpr double CEM_W_INVALID{1'000'000.0};

// "Cold start" PID seed used as the advisor's very first (pre-decision)
// operating point and as the horizon cost's control-change reference
// point -- the midpoint of each gain's search range.
constexpr double COLD_START_KP{25.0};
constexpr double COLD_START_KI{500.0};
constexpr double COLD_START_KD{10.0};

constexpr int    CEM_POPULATION{256};
constexpr int    CEM_ITERATIONS{3};
constexpr double CEM_ELITE_FRAC{0.12};
constexpr double CEM_MIN_STD_FRAC{0.03};

struct CemResult {
  bool                     valid{false};
  double                   kp{COLD_START_KP};
  double                   ki{COLD_START_KI};
  double                   kd{COLD_START_KD};
  double                   cost{0.0};
  QVector<TrajectoryPoint> trajectory;
  SimMetrics               metrics; // Simulator-style metrics, for apples-to-apples comparison

  // The median-cost candidate across the whole search population: "what a
  // typical fixed-PID choice would have looked like", as opposed to `kp`/
  // `ki`/`kd` above which is the best one found.
  bool                     medianValid{false};
  double                   medianKp{COLD_START_KP};
  double                   medianKi{COLD_START_KI};
  double                   medianKd{COLD_START_KD};
  QVector<TrajectoryPoint> medianTrajectory;
  SimMetrics               medianMetrics;
};

// One full-duration, fixed-PID-triplet CEM search over the digital twin,
// mirroring foresight's `advisor/advise_pid.py --mode suggest`
// (optimize_pid_batched): finds a single (kp, ki, kd) that's best for the
// whole run, then rolls it out. Every iteration's population includes the
// cold-start triplet as a guaranteed candidate, and the final
// recommendation is only returned if it actually beats the cold-start
// triplet's own rolled-out cost -- otherwise the cold-start triplet itself
// is returned unchanged. Not ported: foresight also seeds each population
// with candidates drawn from a historical run-outcome database, which this
// app has no equivalent of. Each iteration's population is rolled out as
// one batch (one ONNX call per timestep for the whole population, rather
// than one call per candidate). Synchronous/blocking -- run from a worker
// thread.
class CemOptimizer {
public:
  explicit CemOptimizer(GruOnnxModel *model);

  // progressCb(stepsDone, stepsTotal), called after each batched timestep;
  // optional.
  CemResult run(SimParams const &params, std::function<void(int, int)> const &progressCb = {}) const;

private:
  bool rolloutPopulation(SimParams const &params, QVector<std::array<double, 3>> const &candidates,
                          QVector<QVector<TrajectoryPoint>> &trajectoriesOut, QVector<bool> &validOut,
                          std::function<void(int, int)> const &stepProgressCb) const;
  double horizonCost(QVector<TrajectoryPoint> const &trajectory, SimParams const &params,
                      double candidateKp, double candidateKi, double candidateKd, double previousKp,
                      double previousKi, double previousKd, bool valid) const;

  GruOnnxModel *m_model;
};

} // namespace control2::ai

#endif // ADVISORCEM_H
