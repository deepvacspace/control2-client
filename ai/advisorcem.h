#ifndef ADVISORCEM_H
#define ADVISORCEM_H

#include <QString>
#include <QVector>
#include <functional>

#include "chambertwinsim.h"
#include "gruonnxmodel.h"

namespace control2::ai {

// Cost weights mirrored from foresight's advisor/advise_pid.py::add_cost_args
// (the "horizon_cost" used by both the CEM optimizer and PPO's training
// reward) -- distinct from the Simulator tab's own cost weights.
constexpr double CEM_W_OVERSHOOT_MAX{100.0};
constexpr double CEM_W_OVERSHOOT_RMSE{30.0};
constexpr double CEM_W_ABS_ERROR{0.2};
constexpr double CEM_W_FINAL_ABS_ERROR{0.5};
constexpr double CEM_W_NEAR_STD{2.0};
constexpr double CEM_W_CONTROL_CHANGE{0.01};
constexpr double CEM_W_INVALID{1'000'000.0};

// "Cold start" PID seed used both as the CEM search's initial candidate and
// as the advisor's very first (pre-decision) operating point -- mirrors
// deepvac/mpc.py::add_common_mpc_args's --kp-init/--ki-init/--kd-init
// defaults.
constexpr double COLD_START_KP{6.0};
constexpr double COLD_START_KI{997.0};
constexpr double COLD_START_KD{16.0};

// Population/iteration counts are reduced from foresight's CEM defaults
// (population=256, iterations=3) since this port evaluates candidates one
// at a time (no batched ONNX inference); this keeps a full search to a low
// tens-of-seconds range on CPU instead of several minutes.
constexpr int    CEM_POPULATION{64};
constexpr int    CEM_ITERATIONS{2};
constexpr double CEM_ELITE_FRAC{0.12};
constexpr double CEM_MIN_STD_FRAC{0.03};
constexpr double CEM_INITIAL_STD_FRAC{0.40};

struct CemResult {
  bool                     valid{false};
  double                   kp{COLD_START_KP};
  double                   ki{COLD_START_KI};
  double                   kd{COLD_START_KD};
  double                   cost{0.0};
  QVector<TrajectoryPoint> trajectory;
  SimMetrics               metrics; // Simulator-style metrics, for apples-to-apples comparison
};

// One full-duration, fixed-PID-triplet CEM search over the digital twin,
// mirroring foresight's `advisor/advise_pid.py --mode suggest`: finds a
// single (kp, ki, kd) that's best for the whole run, then rolls it out.
// This is the "what would an average/near-optimal fixed-PID run have
// looked like" baseline. Synchronous/blocking -- run from a worker thread.
class CemOptimizer {
public:
  explicit CemOptimizer(GruOnnxModel *model);

  // progressCb(candidatesEvaluated, candidatesTotal), called after each
  // candidate rollout; optional.
  CemResult run(SimParams const &params, std::function<void(int, int)> const &progressCb = {}) const;

private:
  bool rolloutCandidate(SimParams const &params, double kp, double ki, double kd,
                         QVector<TrajectoryPoint> &trajectoryOut) const;
  double horizonCost(QVector<TrajectoryPoint> const &trajectory, SimParams const &params,
                      double candidateKp, double candidateKi, double candidateKd, double previousKp,
                      double previousKi, double previousKd, bool valid) const;

  GruOnnxModel *m_model;
};

} // namespace control2::ai

#endif // ADVISORCEM_H
