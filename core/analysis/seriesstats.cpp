#include "seriesstats.h"

#include <algorithm>
#include <cmath>

namespace deepvaccontrol::core {

double percentile(QVector<double> values, double q) {
  values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !std::isfinite(v); }), values.end());
  if (values.isEmpty()) {
    return NaN;
  }
  std::sort(values.begin(), values.end());
  double const    position{std::clamp(q, 0.0, 1.0) * static_cast<double>(values.size() - 1)};
  qsizetype const lower{static_cast<qsizetype>(std::floor(position))};
  qsizetype const upper{std::min(lower + 1, values.size() - 1)};
  return values[lower] + (values[upper] - values[lower]) * (position - static_cast<double>(lower));
}

SeriesStats computeStats(QVector<double> const &times, QVector<double> const &values, double from, double to) {
  SeriesStats     stats;
  qsizetype const n{std::min(times.size(), values.size())};

  QVector<double> inRange;
  double          sum{0.0};
  double          sumSq{0.0};
  double          sumT{0.0};
  double          sumTT{0.0};
  double          sumTY{0.0};
  double          tRef{NaN}; // centring the times keeps the slope numerically stable

  for (qsizetype i{0}; i < n; ++i) {
    double const t{times[i]};
    if ((std::isfinite(from) && (t < from)) || (std::isfinite(to) && (t > to))) {
      continue;
    }
    double const y{values[i]};
    if (!std::isfinite(y)) {
      ++stats.missing;
      continue;
    }
    if (!std::isfinite(tRef)) {
      tRef = t;
    }
    double const dt{t - tRef};
    inRange.push_back(y);
    sum += y;
    sumSq += y * y;
    sumT += dt;
    sumTT += dt * dt;
    sumTY += dt * y;
  }

  stats.n = inRange.size();
  if (stats.n == 0) {
    return stats;
  }
  double const count{static_cast<double>(stats.n)};
  auto const [minIt, maxIt] = std::minmax_element(inRange.cbegin(), inRange.cend());
  stats.min  = *minIt;
  stats.max  = *maxIt;
  stats.mean = sum / count;
  stats.rms  = std::sqrt(sumSq / count);
  if (stats.n > 1) {
    double variance{0.0};
    for (double const y : std::as_const(inRange)) {
      variance += (y - stats.mean) * (y - stats.mean);
    }
    stats.std = std::sqrt(variance / (count - 1.0));
    double const denominator{count * sumTT - sumT * sumT};
    if (denominator > 0.0) {
      stats.slopePerS = (count * sumTY - sumT * sum) / denominator;
    }
  }
  stats.p95 = percentile(std::move(inRange), 0.95);
  return stats;
}

}
