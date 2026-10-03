#include "anomalies.h"

#include <algorithm>
#include <cmath>

#include "seriesstats.h"

namespace deepvaccontrol::core {

namespace {
QString number(double value, int precision = 3) {
  return QString::number(value, 'g', precision);
}

double typicalPeriod(QVector<double> const &times) {
  QVector<double> steps;
  for (qsizetype i{1}; i < times.size(); ++i) {
    steps.push_back(times[i] - times[i - 1]);
  }
  return steps.isEmpty() ? NaN : percentile(steps, 0.5);
}
} // namespace

QString Anomaly::kindName(Kind kind) {
  switch (kind) {
    case Kind::LIMIT:
      return QStringLiteral("limit");
    case Kind::SPIKE:
      return QStringLiteral("spike");
    case Kind::FLATLINE:
      return QStringLiteral("flatline");
    case Kind::GAP:
      return QStringLiteral("gap");
    case Kind::OSCILLATION:
      return QStringLiteral("oscillation");
    case Kind::NOT_REACHED:
      return QStringLiteral("not_reached");
    default:
      return QStringLiteral("saturation");
  }
}

QString Anomaly::severityName(Severity severity) {
  switch (severity) {
    case Severity::INFO:
      return QStringLiteral("info");
    case Severity::WARNING:
      return QStringLiteral("warning");
    default:
      return QStringLiteral("critical");
  }
}

QVector<Anomaly> detectLimitViolations(QVector<double> const &times, QVector<double> const &values, LimitRule const &rule) {
  QVector<Anomaly> found;
  qsizetype const  n{std::min(times.size(), values.size())};
  qsizetype        begin{-1};
  double           worst{NaN};
  auto             excursion = [&](double v) {
    double over{0.0};
    if (std::isfinite(rule.hi) && (v > rule.hi)) {
      over = v - rule.hi;
    }
    if (std::isfinite(rule.lo) && (v < rule.lo)) {
      over = std::max(over, rule.lo - v);
    }
    return over;
  };
  for (qsizetype i{0}; i <= n; ++i) {
    bool const outside{(i < n) && std::isfinite(values[i]) && (excursion(values[i]) > 0.0)};
    if (outside) {
      if (begin < 0) {
        begin = i;
        worst = values[i];
      } else if (excursion(values[i]) > excursion(worst)) {
        worst = values[i];
      }
    } else if (begin >= 0) {
      Anomaly anomaly;
      anomaly.kind     = Anomaly::Kind::LIMIT;
      anomaly.severity = Anomaly::Severity::WARNING;
      anomaly.channel  = rule.channel;
      anomaly.start    = times[begin];
      anomaly.end      = times[i - 1];
      anomaly.value    = worst;
      anomaly.detail   = QStringLiteral("%1: %2 outside [%3, %4]")
                         .arg(rule.label.isEmpty() ? rule.channel : rule.label, number(worst, 5),
                              std::isfinite(rule.lo) ? number(rule.lo, 5) : QStringLiteral("-inf"),
                              std::isfinite(rule.hi) ? number(rule.hi, 5) : QStringLiteral("+inf"));
      found.push_back(anomaly);
      begin = -1;
    }
  }
  return found;
}

QVector<Anomaly> detectSpikes(QVector<double> const &times, QVector<double> const &values, QString const &channel,
                              AnomalyOptions const &options) {
  qsizetype const n{std::min(times.size(), values.size())};
  QVector<double> deltas(n, NaN);
  QVector<double> valid;
  for (qsizetype i{1}; i < n; ++i) {
    if (std::isfinite(values[i]) && std::isfinite(values[i - 1])) {
      deltas[i] = values[i] - values[i - 1];
      valid.push_back(deltas[i]);
    }
  }
  if (valid.size() < 20) {
    return {};
  }
  double const    median{percentile(valid, 0.5)};
  QVector<double> deviations;
  for (double const delta : std::as_const(valid)) {
    deviations.push_back(std::fabs(delta - median));
  }
  double scale{1.4826 * percentile(deviations, 0.5)};
  if (!(scale > 0.0)) {
    // quantised / very quiet signal: fall back to the mean absolute deviation
    double sum{0.0};
    for (double const deviation : std::as_const(deviations)) {
      sum += deviation;
    }
    scale = 1.2533 * sum / deviations.size();
  }
  if (!(scale > 0.0)) {
    return {};
  }
  // a spike must also be a sizeable jump for the channel (very quiet signals have a tiny scale)
  QVector<double> levels;
  for (qsizetype i{0}; i < n; ++i) {
    if (std::isfinite(values[i])) {
      levels.push_back(values[i]);
    }
  }
  double const minJump{std::max(options.spikeMinJump, options.spikeMinJumpOfSpan * (percentile(levels, 0.99) - percentile(levels, 0.01)))};

  QVector<Anomaly> found;
  for (qsizetype i{1}; i < n; ++i) {
    if (!std::isfinite(deltas[i])) {
      continue;
    }
    double const z{(deltas[i] - median) / scale};
    if ((std::fabs(z) < options.spikeZ) || (std::fabs(deltas[i]) < minJump)) {
      continue;
    }
    // a spike comes back: a later change of the opposite sign undoes most of it
    qsizetype revert{-1};
    for (qsizetype j{i + 1}; (j < n) && (j <= i + options.spikeRevertSamples); ++j) {
      if (std::isfinite(deltas[j]) && (deltas[j] * deltas[i] < 0.0) && (std::fabs(values[j] - values[i - 1]) <= 0.5 * std::fabs(deltas[i]))) {
        revert = j;
        break;
      }
    }
    if (revert < 0) {
      continue; // a step, not a spike
    }
    if (!found.isEmpty() && (times[i] - found.last().end <= 0.0)) {
      continue;
    }
    Anomaly anomaly;
    anomaly.kind     = Anomaly::Kind::SPIKE;
    anomaly.severity = Anomaly::Severity::WARNING;
    anomaly.channel  = channel;
    anomaly.start    = times[i];
    anomaly.end      = times[revert - 1];
    anomaly.value    = values[i];
    anomaly.detail   = QStringLiteral("%1 jumped by %2 (robust z %3) and came back").arg(channel, number(deltas[i]), number(z, 2));
    found.push_back(anomaly);
    i = revert;
  }
  return found;
}

QVector<Anomaly> detectFlatlines(QVector<double> const &times, QVector<double> const &values, QString const &channel,
                                 AnomalyOptions const &options) {
  qsizetype const n{std::min(times.size(), values.size())};
  QVector<Anomaly> found;
  qsizetype        begin{-1};
  for (qsizetype i{0}; i <= n; ++i) {
    bool const same{(i < n) && (begin >= 0) && std::isfinite(values[i]) && (values[i] == values[begin])};
    if (same) {
      continue;
    }
    if ((begin >= 0) && (times[i - 1] - times[begin] >= options.flatlineS)) {
      Anomaly anomaly;
      anomaly.kind     = Anomaly::Kind::FLATLINE;
      anomaly.severity = Anomaly::Severity::INFO;
      anomaly.channel  = channel;
      anomaly.start    = times[begin];
      anomaly.end      = times[i - 1];
      anomaly.value    = values[begin];
      anomaly.detail   = QStringLiteral("%1 stuck at %2 for %3 s").arg(channel, number(values[begin], 5), number(anomaly.end - anomaly.start, 4));
      found.push_back(anomaly);
    }
    begin = ((i < n) && std::isfinite(values[i])) ? i : -1;
  }
  return found;
}

QVector<Anomaly> detectGaps(QVector<double> const &times, QVector<double> const &values, QString const &channel,
                            AnomalyOptions const &options) {
  QVector<Anomaly> found;
  double const     period{typicalPeriod(times)};
  if (!std::isfinite(period)) {
    return found;
  }
  double const threshold{std::max(options.minGapS, options.gapFactor * period)};
  auto         add = [&](double start, double end, QString const &detail) {
    Anomaly anomaly;
    anomaly.kind     = Anomaly::Kind::GAP;
    anomaly.severity = Anomaly::Severity::INFO;
    anomaly.channel  = channel;
    anomaly.start    = start;
    anomaly.end      = end;
    anomaly.value    = end - start;
    anomaly.detail   = detail;
    found.push_back(anomaly);
  };
  for (qsizetype i{1}; i < times.size(); ++i) {
    if (times[i] - times[i - 1] >= threshold) {
      add(times[i - 1], times[i], QStringLiteral("no samples for %1 s").arg(number(times[i] - times[i - 1], 4)));
    }
  }
  qsizetype const n{std::min(times.size(), values.size())};
  qsizetype       begin{-1};
  for (qsizetype i{0}; i <= n; ++i) {
    bool const missing{(i < n) && !std::isfinite(values[i])};
    if (missing && (begin < 0)) {
      begin = i;
    } else if (!missing && (begin >= 0)) {
      double const start{times[begin]};
      double const end{i < n ? times[i] : times[n - 1]};
      if (end - start >= threshold) {
        add(start, end, QStringLiteral("%1 missing for %2 s").arg(channel, number(end - start, 4)));
      }
      begin = -1;
    }
  }
  return found;
}

QVector<Anomaly> detectOscillation(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference,
                                   AnomalyOptions const &options) {
  qsizetype const n{std::min({times.size(), value.size(), reference.size()})};
  QVector<double> crossings; // times the error changed side
  int             side{0};
  for (qsizetype i{0}; i < n; ++i) {
    double const error{reference[i] - value[i]};
    if (!std::isfinite(error) || (std::fabs(error) <= options.crossingDeadband)) {
      continue;
    }
    int const now{error > 0.0 ? 1 : -1};
    if ((side != 0) && (now != side)) {
      crossings.push_back(times[i]);
    }
    side = now;
  }

  QVector<Anomaly> found;
  qsizetype        lo{0};
  for (qsizetype i{0}; i < crossings.size(); ++i) {
    while (crossings[lo] < crossings[i] - options.oscillationWindowS) {
      ++lo;
    }
    if (i - lo + 1 < options.oscillationCrossings) {
      continue;
    }
    double const start{crossings[lo]};
    if (!found.isEmpty() && (start <= found.last().end)) {
      found.last().end   = crossings[i];
      found.last().value = std::max(found.last().value, static_cast<double>(i - lo + 1));
    } else {
      Anomaly anomaly;
      anomaly.kind     = Anomaly::Kind::OSCILLATION;
      anomaly.severity = Anomaly::Severity::WARNING;
      anomaly.channel  = QStringLiteral("temp");
      anomaly.start    = start;
      anomaly.end      = crossings[i];
      anomaly.value    = static_cast<double>(i - lo + 1);
      found.push_back(anomaly);
    }
  }
  for (Anomaly &anomaly : found) {
    anomaly.detail = QStringLiteral("crossed the setpoint %1 times within %2 s").arg(number(anomaly.value), number(options.oscillationWindowS));
  }
  return found;
}

QVector<Anomaly> detectUnreachedSetpoints(RunAnalysis const &analysis, AnomalyOptions const &options) {
  QVector<Anomaly> found;
  for (StageAnalysis const &stage : analysis.stages) {
    if ((stage.segment.kind != Segment::Kind::SETPOINT) || std::isfinite(stage.kpis.settlingTimeS)) {
      continue;
    }
    bool const neverNear{!std::isfinite(stage.kpis.timeToNearS)};
    Anomaly    anomaly;
    anomaly.kind     = Anomaly::Kind::NOT_REACHED;
    anomaly.severity = neverNear ? Anomaly::Severity::CRITICAL : Anomaly::Severity::WARNING;
    anomaly.channel  = QStringLiteral("temp");
    anomaly.start    = stage.segment.start;
    anomaly.end      = stage.segment.end;
    anomaly.value    = stage.segment.referenceEnd;
    anomaly.detail   = neverNear ? QStringLiteral("setpoint %1 never approached (closest %2)")
                                   .arg(number(stage.segment.referenceEnd, 5),
                                        number(std::fabs(stage.segment.referenceEnd - stage.kpis.endValue), 3))
                                 : QStringLiteral("setpoint %1 approached but never settled within the band")
                                     .arg(number(stage.segment.referenceEnd, 5));
    Q_UNUSED(options);
    found.push_back(anomaly);
  }
  return found;
}

QVector<Anomaly> detectLongSaturation(QVector<SaturationEpisode> const &episodes, AnomalyOptions const &options) {
  QVector<Anomaly> found;
  for (SaturationEpisode const &episode : episodes) {
    if (episode.end - episode.start < options.saturationS) {
      continue;
    }
    Anomaly anomaly;
    anomaly.kind     = Anomaly::Kind::SATURATION;
    anomaly.severity = Anomaly::Severity::WARNING;
    anomaly.channel  = QStringLiteral("temp_u");
    anomaly.start    = episode.start;
    anomaly.end      = episode.end;
    anomaly.value    = episode.end - episode.start;
    anomaly.detail   = QStringLiteral("control output at its %1 limit for %2 s")
                       .arg(episode.high ? QStringLiteral("upper") : QStringLiteral("lower"), number(anomaly.value, 4));
    found.push_back(anomaly);
  }
  return found;
}

} // namespace deepvaccontrol::core
