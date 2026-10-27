#include "kpis.h"

#include <algorithm>
#include <cmath>

namespace deepvaccontrol::core {

namespace {
bool valid(double v) {
  return std::isfinite(v);
}

// time when `value` first crosses `level` going in `direction`, interpolated between samples
double crossingTime(QVector<double> const &t, QVector<double> const &v, QVector<int> const &idx, double level, int direction) {
  for (qsizetype k{0}; k < idx.size(); ++k) {
    int const    i{idx[k]};
    double const d{(v[i] - level) * direction};
    if (d >= 0.0) {
      if (k == 0) {
        return t[i];
      }
      int const    j{idx[k - 1]};
      double const prev{(v[j] - level) * direction};
      double const fraction{prev / (prev - d)}; // prev < 0 <= d
      return t[j] + (t[i] - t[j]) * fraction;
    }
  }
  return NaN;
}
}

ControlKpis computeKpis(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference,
                        KpiOptions const &options) {
  ControlKpis kpis;
  qsizetype const n{std::min({times.size(), value.size(), reference.size()})};

  QVector<int> idx; // samples with value and reference
  idx.reserve(n);
  for (qsizetype i{0}; i < n; ++i) {
    if (valid(times[i]) && valid(value[i]) && valid(reference[i])) {
      idx.push_back(static_cast<int>(i));
    }
  }
  kpis.samples = static_cast<int>(idx.size());
  if (idx.isEmpty()) {
    return kpis;
  }

  double const t0{valid(options.startTimeS) ? options.startTimeS : times[idx.first()]};
  double const tEnd{valid(options.tailEndS) ? options.tailEndS : times[idx.last()]};
  double const tailStart{std::max(t0, tEnd - options.tailWindowS)};
  double const y0{valid(options.initialValue) ? options.initialValue : value[idx.first()]};

  bool anyTail{false};
  for (int const i : idx) {
    if (times[i] >= tailStart) {
      anyTail = true;
      break;
    }
  }

  double sumAbs{0.0};
  double sumSq{0.0};
  double sumErr{0.0};
  double maxAbs{0.0};
  double overshoot{0.0};
  double sumOvershootSq{0.0};
  double undershoot{0.0};
  bool   reached{false};
  double tailSumAbs{0.0};
  double tailSumErr{0.0};
  double tailSum{0.0};
  double tailSumSq{0.0};
  int    tailCount{0};
  double nearSum{0.0};
  double nearSumSq{0.0};
  int    nearCount{0};
  double minValue{value[idx.first()]};
  double maxValue{value[idx.first()]};
  int    lastOutsideSettle{-1}; // position in idx of the last sample outside the settle band

  for (qsizetype k{0}; k < idx.size(); ++k) {
    int const    i{idx[k]};
    double const y{value[i]};
    double const r{reference[i]};
    double const error{r - y};
    double const absError{std::fabs(error)};
    int const    direction{(r > y0) ? 1 : -1}; // same rule as the earlier implementations (equal -> cooling)

    sumAbs += absError;
    sumSq += error * error;
    sumErr += error;
    maxAbs = std::max(maxAbs, absError);
    minValue = std::min(minValue, y);
    maxValue = std::max(maxValue, y);

    double const beyond{std::max(direction > 0 ? y - r : r - y, 0.0)}; // past the reference, approach direction
    overshoot = std::max(overshoot, beyond);
    sumOvershootSq += beyond * beyond;
    if (reached) {
      undershoot = std::max(undershoot, std::max(direction > 0 ? r - y : y - r, 0.0));
    } else if ((direction > 0 ? y >= r : y <= r)) {
      reached = true;
    }

    if (!anyTail || (times[i] >= tailStart)) {
      tailSumAbs += absError;
      tailSumErr += error;
      tailSum += y;
      tailSumSq += y * y;
      ++tailCount;
    }
    if (absError <= options.nearBand) {
      if (!valid(kpis.timeToNearS)) {
        kpis.timeToNearS = times[i] - t0;
      }
      nearSum += y;
      nearSumSq += y * y;
      ++nearCount;
    }
    if (absError > options.settleBand) {
      lastOutsideSettle = static_cast<int>(k);
    }
  }

  double const count{static_cast<double>(idx.size())};
  kpis.mae          = sumAbs / count;
  kpis.rmse         = std::sqrt(sumSq / count);
  kpis.bias         = sumErr / count;
  kpis.maxAbsError  = maxAbs;
  kpis.overshoot    = overshoot;
  kpis.overshootRms = std::sqrt(sumOvershootSq / count);
  kpis.undershoot   = reached ? undershoot : NaN;
  kpis.minValue     = minValue;
  kpis.maxValue     = maxValue;
  kpis.endValue     = value[idx.last()];
  kpis.finalError   = reference[idx.last()] - value[idx.last()];

  if (tailCount > 0) {
    double const mean{tailSum / tailCount};
    kpis.tailMae  = tailSumAbs / tailCount;
    kpis.tailBias = tailSumErr / tailCount;
    kpis.tailStd  = std::sqrt(std::max(0.0, tailSumSq / tailCount - mean * mean));
  }
  if (nearCount > 0) {
    double const mean{nearSum / nearCount};
    kpis.nearStd = std::sqrt(std::max(0.0, nearSumSq / nearCount - mean * mean));
  }

  // settling: enter the band and stay in it until the end
  if (lastOutsideSettle < static_cast<int>(idx.size()) - 1) {
    int const settledAt{idx[lastOutsideSettle + 1]};
    kpis.settlingTimeS = times[settledAt] - t0;
    double sum{0.0};
    int    settledCount{0};
    for (qsizetype k{lastOutsideSettle + 1}; k < idx.size(); ++k) {
      sum += reference[idx[k]] - value[idx[k]];
      ++settledCount;
    }
    kpis.steadyStateError = sum / settledCount;
  }

  // rise / fall time: 10 -> 90 % of the move from the initial value to the final reference
  double const target{reference[idx.last()]};
  double const span{target - y0};
  kpis.direction = (span > 0.0) ? 1 : ((span < 0.0) ? -1 : 0);
  if (kpis.direction != 0) {
    double const low{crossingTime(times, value, idx, y0 + options.riseLow * span, kpis.direction)};
    double const high{crossingTime(times, value, idx, y0 + options.riseHigh * span, kpis.direction)};
    double const transition{(valid(low) && valid(high)) ? high - low : NaN};
    (kpis.direction > 0 ? kpis.riseTimeS : kpis.fallTimeS) = transition;
  }
  return kpis;
}

ControlKpis computeKpis(QVector<double> const &times, QVector<double> const &value, double reference,
                        KpiOptions const &options) {
  return computeKpis(times, value, QVector<double>(times.size(), reference), options);
}

double controlCost(ControlKpis const &kpis, bool invalid, CostWeights const &weights) {
  auto orZero = [](double v) {
    return std::isfinite(v) ? v : 0.0;
  };
  double cost = weights.tailMae * orZero(kpis.tailMae) + weights.overshoot * orZero(kpis.overshoot)
                + weights.tailStd * orZero(kpis.tailStd) + weights.finalError * std::fabs(orZero(kpis.finalError));
  if (invalid) {
    cost += weights.invalid;
  }
  return cost;
}

}
