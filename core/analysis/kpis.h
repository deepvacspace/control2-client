#ifndef CORE_ANALYSIS_KPIS_H
#define CORE_ANALYSIS_KPIS_H

#include <QVector>
#include <limits>

// Control-loop performance indicators of a measured (or simulated) value following a reference.
// Pure computation (QtCore containers only): shared by the analysis UI, the experiment database,
// the AI digital twin / advisors and reports.
namespace deepvaccontrol::core {

inline constexpr double NaN{std::numeric_limits<double>::quiet_NaN()};

struct KpiOptions {
  double settleBand{0.5};   // |error| band for settling time / steady state, same unit as the value
  double nearBand{2.0};     // |error| band for "time to near"
  double tailWindowS{300.0}; // tail statistics: the last N seconds
  double tailEndS{NaN};     // tail window ends here (elapsed s); NaN = last sample
  double initialValue{NaN}; // value the transition starts from; NaN = first valid sample
  double startTimeS{NaN};   // times (settling, time to near) are measured from here; NaN = first valid sample
  double riseLow{0.1};      // rise / fall time from 10 % ...
  double riseHigh{0.9};     // ... to 90 % of the transition
};

/**
 * Error convention: error = reference - value (positive when the value is below the reference).
 * Direction: +1 when the reference is above the initial value (heating), else -1 (cooling).
 * Every value is NaN when it cannot be computed (no valid samples, never settled, no transition).
 */
struct ControlKpis {
  int    samples{0};          // samples with both value and reference
  double mae{NaN};            // mean |error|
  double rmse{NaN};
  double bias{NaN};           // mean error
  double maxAbsError{NaN};
  double overshoot{NaN};      // max excursion beyond the reference in the approach direction (>= 0)
  double overshootRms{NaN};   // RMS of that excursion over all samples
  double undershoot{NaN};     // max excursion back below / above the reference after first reaching it (>= 0)
  double riseTimeS{NaN};      // 10 -> 90 % of the transition when heating
  double fallTimeS{NaN};      // 10 -> 90 % of the transition when cooling
  double timeToNearS{NaN};    // first time |error| <= nearBand
  double settlingTimeS{NaN};  // from the start until |error| enters settleBand and stays there to the end
  double steadyStateError{NaN}; // mean error after settling (NaN if it never settles)
  double tailMae{NaN};        // over the tail window
  double tailBias{NaN};
  double tailStd{NaN};        // population std of the value over the tail window
  double nearStd{NaN};        // std of the value while |error| <= nearBand
  double finalError{NaN};
  double minValue{NaN};
  double maxValue{NaN};
  double endValue{NaN};
  int    direction{0};
};

// times: elapsed seconds (ascending); value / reference: same length, NaN = missing sample
ControlKpis computeKpis(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference,
                        KpiOptions const &options = {});

// Same, for a constant reference (simulations, single setpoint)
ControlKpis computeKpis(QVector<double> const &times, QVector<double> const &value, double reference,
                        KpiOptions const &options = {});

// Weighted cost used to rank controller settings (lower is better); `invalid` adds a large penalty.
struct CostWeights {
  double tailMae{1.0};
  double overshoot{10.0};
  double tailStd{0.5};
  double finalError{0.5};
  double invalid{1'000'000.0};
};
double controlCost(ControlKpis const &kpis, bool invalid = false, CostWeights const &weights = {});

}

#endif // CORE_ANALYSIS_KPIS_H
