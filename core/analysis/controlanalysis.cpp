#include "controlanalysis.h"

#include <algorithm>
#include <cmath>

#include "seriesstats.h"

namespace deepvaccontrol::core {

namespace {
// time a sample stands for: up to the next sample, gaps capped at a few typical periods
QVector<double> sampleDurations(QVector<double> const &times, int first, int last) {
  QVector<double> steps;
  for (int k{first}; k < last; ++k) {
    steps.push_back(times[k + 1] - times[k]);
  }
  double const typical{steps.isEmpty() ? 1.0 : std::max(percentile(steps, 0.5), 1e-9)};
  QVector<double> durations(last - first + 1, typical);
  for (int k{first}; k < last; ++k) {
    durations[k - first] = std::min(times[k + 1] - times[k], 5.0 * typical);
  }
  return durations;
}
} // namespace

SaturationStats analyseSaturation(QVector<double> const &times, QVector<double> const &u, QVector<double> const &integral,
                                  int first, int last, SaturationOptions const &options, QVector<SaturationEpisode> *episodes) {
  SaturationStats stats;
  first = std::max(first, 0);
  last  = std::min<int>(last, static_cast<int>(std::min(times.size(), u.size())) - 1);
  if (last < first) {
    return stats;
  }
  QVector<double> const durations = sampleDurations(times, first, last);
  bool const            withIntegral{integral.size() > last};

  double total{0.0}, high{0.0}, low{0.0}, windup{0.0}, sum{0.0};
  double episodeStart{NaN}, longest{0.0};
  int    episodeSide{0}; // +1 high, -1 low, 0 none
  auto   closeEpisode = [&](double end) {
    if (episodeSide != 0) {
      longest = std::max(longest, end - episodeStart);
      ++stats.episodes;
      if (episodes) {
        episodes->push_back({episodeStart, end, episodeSide > 0});
      }
    }
    episodeSide = 0;
  };

  for (int k{first}; k <= last; ++k) {
    double const value{u[k]};
    if (!std::isfinite(value)) {
      closeEpisode(times[k]);
      continue;
    }
    double const dt{durations[k - first]};
    ++stats.samples;
    total += dt;
    sum += value;
    stats.uMin = std::isfinite(stats.uMin) ? std::min(stats.uMin, value) : value;
    stats.uMax = std::isfinite(stats.uMax) ? std::max(stats.uMax, value) : value;

    int const side{value >= options.uMax - options.tolerance ? 1 : value <= options.uMin + options.tolerance ? -1 : 0};
    if (side > 0) {
      high += dt;
    } else if (side < 0) {
      low += dt;
    }
    if (side != episodeSide) {
      closeEpisode(times[k]);
      if (side != 0) {
        episodeSide  = side;
        episodeStart = times[k];
      }
    }
    if ((side != 0) && withIntegral && (k > first) && std::isfinite(integral[k]) && std::isfinite(integral[k - 1])
        && ((integral[k] - integral[k - 1]) * side > 0.0)) {
      windup += dt;
    }
  }
  closeEpisode(times[last]);

  if (stats.samples > 0) {
    stats.fractionHigh = high / total;
    stats.fractionLow  = low / total;
    stats.fraction     = (high + low) / total;
    stats.totalS       = high + low;
    stats.longestS     = longest;
    stats.uMean        = sum / stats.samples;
    stats.windupS      = withIntegral ? windup : NaN;
  }
  return stats;
}

QString PidContribution::dominant() const {
  if (!(std::isfinite(shareP) && std::isfinite(shareI) && std::isfinite(shareD))) {
    return {};
  }
  if ((shareP >= shareI) && (shareP >= shareD)) {
    return QStringLiteral("P");
  }
  return (shareI >= shareD) ? QStringLiteral("I") : QStringLiteral("D");
}

PidContribution analysePidContribution(QVector<double> const &p, QVector<double> const &i, QVector<double> const &d, int first,
                                       int last) {
  PidContribution contribution;
  first = std::max(first, 0);
  last  = std::min<int>(last, static_cast<int>(std::min({p.size(), i.size(), d.size()})) - 1);
  double sumP{0.0}, sumI{0.0}, sumD{0.0}, absP{0.0}, absI{0.0}, absD{0.0};
  for (int k{first}; k <= last; ++k) {
    if (!(std::isfinite(p[k]) && std::isfinite(i[k]) && std::isfinite(d[k]))) {
      continue;
    }
    ++contribution.samples;
    sumP += p[k];
    sumI += i[k];
    sumD += d[k];
    absP += std::fabs(p[k]);
    absI += std::fabs(i[k]);
    absD += std::fabs(d[k]);
  }
  if (contribution.samples == 0) {
    return contribution;
  }
  double const n{static_cast<double>(contribution.samples)};
  contribution.meanP    = sumP / n;
  contribution.meanI    = sumI / n;
  contribution.meanD    = sumD / n;
  contribution.meanAbsP = absP / n;
  contribution.meanAbsI = absI / n;
  contribution.meanAbsD = absD / n;
  double const total{absP + absI + absD};
  if (total > 0.0) {
    contribution.shareP = absP / total;
    contribution.shareI = absI / total;
    contribution.shareD = absD / total;
  }
  return contribution;
}

} // namespace deepvaccontrol::core
