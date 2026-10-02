#ifndef CORE_ANALYSIS_SERIESSTATS_H
#define CORE_ANALYSIS_SERIESSTATS_H

#include <QVector>

#include "kpis.h"

namespace deepvaccontrol::core {

// Descriptive statistics of one channel over a time interval.
struct SeriesStats {
  qint64 n{0};       // valid samples in the interval
  qint64 missing{0}; // samples in the interval without a value
  double min{NaN};
  double max{NaN};
  double mean{NaN};
  double std{NaN};   // sample standard deviation (n - 1)
  double rms{NaN};
  double slopePerS{NaN}; // least-squares trend, value units per second
  double p95{NaN};       // 95th percentile, linear interpolation between samples
};

// times ascending (s); [from, to] inclusive, NaN bounds = unbounded
SeriesStats computeStats(QVector<double> const &times, QVector<double> const &values, double from = NaN, double to = NaN);

// q in [0, 1], linear interpolation between order statistics (numpy's default)
double percentile(QVector<double> values, double q);

} // namespace deepvaccontrol::core

#endif // CORE_ANALYSIS_SERIESSTATS_H
