#ifndef CORE_ANALYSIS_STEADYSTATE_H
#define CORE_ANALYSIS_STEADYSTATE_H

#include <QVector>

#include "kpis.h"

namespace deepvaccontrol::core {

struct SteadyStateOptions {
  double windowS{120.0};       // the criteria are evaluated on a trailing window this long
  double maxStd{0.1};          // standard deviation of the value within the window
  double maxSlopePerMin{0.05}; // |least-squares trend| within the window, value units per minute
  double maxAbsError{0.5};     // |mean(reference - value)| within the window (only with a reference)
  double minDurationS{60.0};   // shorter steady stretches are dropped
};

// A stretch where the value is stable (and, with a reference, on target).
struct SteadyInterval {
  int    first{0}; // sample indexes, inclusive
  int    last{0};
  double start{NaN};
  double end{NaN};
  double mean{NaN};
  double std{NaN};
  double meanError{NaN}; // mean(reference - value), NaN without a reference

  [[nodiscard]] double duration() const {
    return end - start;
  }
};

// times ascending (s); reference may be empty
QVector<SteadyInterval> detectSteadyStates(QVector<double> const &times, QVector<double> const &value,
                                           QVector<double> const &reference, SteadyStateOptions const &options = {});

// first time >= from at which a steady interval is in progress (NaN when none starts before `to`)
double firstSteadyTime(QVector<SteadyInterval> const &steady, double from, double to);
// part of [from, to] covered by steady intervals, 0 ... 1 (NaN for an empty range)
double steadyFraction(QVector<SteadyInterval> const &steady, double from, double to);

}

#endif // CORE_ANALYSIS_STEADYSTATE_H
