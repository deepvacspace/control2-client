#include "steadystate.h"

#include <algorithm>
#include <cmath>

namespace deepvaccontrol::core {

QVector<SteadyInterval> detectSteadyStates(QVector<double> const &times, QVector<double> const &value,
                                           QVector<double> const &reference, SteadyStateOptions const &options) {
  qsizetype const n{std::min(times.size(), value.size())};
  if (n < 3) {
    return {};
  }
  bool const   withReference{reference.size() >= n};
  double const t0{times[0]};

  // prefix sums over valid samples (time relative to the first sample for precision)
  QVector<double> count(n + 1, 0.0), st(n + 1, 0.0), sv(n + 1, 0.0), stt(n + 1, 0.0), svv(n + 1, 0.0), stv(n + 1, 0.0);
  QVector<double> errorCount(n + 1, 0.0), errorSum(n + 1, 0.0);
  for (qsizetype i{0}; i < n; ++i) {
    double const t{times[i] - t0};
    double const v{value[i]};
    bool const   valid{std::isfinite(v)};
    count[i + 1] = count[i] + (valid ? 1.0 : 0.0);
    st[i + 1]    = st[i] + (valid ? t : 0.0);
    sv[i + 1]    = sv[i] + (valid ? v : 0.0);
    stt[i + 1]   = stt[i] + (valid ? t * t : 0.0);
    svv[i + 1]   = svv[i] + (valid ? v * v : 0.0);
    stv[i + 1]   = stv[i] + (valid ? t * v : 0.0);
    bool const errorValid{withReference && valid && std::isfinite(reference[i])};
    errorCount[i + 1] = errorCount[i] + (errorValid ? 1.0 : 0.0);
    errorSum[i + 1]   = errorSum[i] + (errorValid ? reference[i] - v : 0.0);
  }

  // every window that meets the criteria marks all of its samples steady
  QVector<int> cover(n + 1, 0);
  qsizetype    lo{0};
  for (qsizetype i{0}; i < n; ++i) {
    while (times[lo] < times[i] - options.windowS) {
      ++lo;
    }
    if (times[i] - times[lo] < 0.5 * options.windowS) {
      continue; // too short a window to judge
    }
    double const c{count[i + 1] - count[lo]};
    if (c < 5.0) {
      continue;
    }
    double const sT{st[i + 1] - st[lo]};
    double const sV{sv[i + 1] - sv[lo]};
    double const variance{std::max(0.0, (svv[i + 1] - svv[lo] - sV * sV / c) / (c - 1.0))};
    double const denominator{c * (stt[i + 1] - stt[lo]) - sT * sT};
    double const slope{denominator > 0.0 ? (c * (stv[i + 1] - stv[lo]) - sT * sV) / denominator : 0.0};
    bool         steady{std::sqrt(variance) <= options.maxStd && std::fabs(slope) * 60.0 <= options.maxSlopePerMin};
    if (steady && withReference) {
      double const ec{errorCount[i + 1] - errorCount[lo]};
      steady = (ec > 0.0) && (std::fabs((errorSum[i + 1] - errorSum[lo]) / ec) <= options.maxAbsError);
    }
    if (steady) {
      ++cover[lo];
      --cover[i + 1];
    }
  }

  QVector<SteadyInterval> intervals;
  int                     depth{0};
  qsizetype               begin{-1};
  for (qsizetype i{0}; i <= n; ++i) {
    bool const steady{(i < n) && ((depth += cover[i]) > 0)};
    if (steady && (begin < 0)) {
      begin = i;
    } else if (!steady && (begin >= 0)) {
      SteadyInterval interval;
      interval.first = static_cast<int>(begin);
      interval.last  = static_cast<int>(i - 1);
      interval.start = times[begin];
      interval.end   = times[i - 1];
      if (interval.duration() >= options.minDurationS) {
        double const c{count[i] - count[begin]};
        double const sV{sv[i] - sv[begin]};
        if (c > 0.0) {
          interval.mean = sV / c;
          interval.std  = (c > 1.0) ? std::sqrt(std::max(0.0, (svv[i] - svv[begin] - sV * sV / c) / (c - 1.0))) : 0.0;
        }
        double const ec{errorCount[i] - errorCount[begin]};
        interval.meanError = (ec > 0.0) ? (errorSum[i] - errorSum[begin]) / ec : NaN;
        intervals.push_back(interval);
      }
      begin = -1;
    }
  }
  return intervals;
}

double firstSteadyTime(QVector<SteadyInterval> const &steady, double from, double to) {
  for (SteadyInterval const &interval : steady) {
    if ((interval.end >= from) && (interval.start <= to)) {
      return std::max(interval.start, from);
    }
  }
  return NaN;
}

double steadyFraction(QVector<SteadyInterval> const &steady, double from, double to) {
  if (!(to > from)) {
    return NaN;
  }
  double covered{0.0};
  for (SteadyInterval const &interval : steady) {
    covered += std::max(0.0, std::min(interval.end, to) - std::max(interval.start, from));
  }
  return std::clamp(covered / (to - from), 0.0, 1.0);
}

} // namespace deepvaccontrol::core
