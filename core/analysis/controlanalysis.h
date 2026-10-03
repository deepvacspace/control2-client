#ifndef CORE_ANALYSIS_CONTROLANALYSIS_H
#define CORE_ANALYSIS_CONTROLANALYSIS_H

#include <QString>
#include <QVector>

#include "kpis.h"

namespace deepvaccontrol::core {

struct SaturationOptions {
  double uMin{-100.0};   // control output limits (temp_u, %)
  double uMax{100.0};
  double tolerance{0.5}; // within this of a limit counts as saturated
};

// How much of the time the controller output sat at its limits.
struct SaturationStats {
  int    samples{0};          // valid output samples
  double fraction{NaN};       // share of time at either limit, 0 ... 1
  double fractionHigh{NaN};   // at uMax
  double fractionLow{NaN};    // at uMin
  double totalS{0.0};         // time at a limit
  double longestS{NaN};       // longest continuous episode
  int    episodes{0};
  double windupS{NaN};        // saturated while the integral term keeps pushing further (integrator windup)
  double uMean{NaN};
  double uMin{NaN};
  double uMax{NaN};
};

// Episode of continuous saturation (for anomaly reporting).
struct SaturationEpisode {
  double start{0.0};
  double end{0.0};
  bool   high{true};
};

// [first, last] sample range (inclusive, clamped); integral (temp_u_i) may be empty
SaturationStats analyseSaturation(QVector<double> const &times, QVector<double> const &u, QVector<double> const &integral,
                                  int first, int last, SaturationOptions const &options = {},
                                  QVector<SaturationEpisode> *episodes = nullptr);

// Share of the P, I and D terms in the controller output.
struct PidContribution {
  int    samples{0};
  double meanP{NaN}; // signed means
  double meanI{NaN};
  double meanD{NaN};
  double meanAbsP{NaN};
  double meanAbsI{NaN};
  double meanAbsD{NaN};
  double shareP{NaN}; // mean |term| / sum of mean |terms|, 0 ... 1
  double shareI{NaN};
  double shareD{NaN};

  [[nodiscard]] QString dominant() const; // "P", "I", "D" or empty
};

PidContribution analysePidContribution(QVector<double> const &p, QVector<double> const &i, QVector<double> const &d, int first,
                                       int last);

} // namespace deepvaccontrol::core

#endif // CORE_ANALYSIS_CONTROLANALYSIS_H
