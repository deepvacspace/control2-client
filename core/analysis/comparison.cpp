#include "comparison.h"

#include <algorithm>
#include <cmath>

namespace deepvaccontrol::core {

namespace {
// starts of the stages where the reference moves to a new level
QVector<int> setpointChanges(ExperimentAnalysis const &analysis, double tolerance) {
  QVector<int>                  changes;
  QVector<StageAnalysis> const &stages = analysis.run.stages;
  for (qsizetype k{1}; k < stages.size(); ++k) {
    Segment const &previous = stages[k - 1].segment;
    Segment const &stage    = stages[k].segment;
    bool const     moves{(stage.kind == Segment::Kind::RAMP && previous.kind != Segment::Kind::RAMP)
                     || ((stage.kind == Segment::Kind::SETPOINT) && (std::fabs(stage.referenceStart - previous.referenceEnd) >= tolerance))};
    if (moves) {
      changes.push_back(static_cast<int>(k));
    }
  }
  return changes;
}

QString text(double value, int precision = 4) {
  return std::isfinite(value) ? QString::number(value, 'g', precision) : QStringLiteral("—");
}
} // namespace

double alignmentAnchor(ExperimentAnalysis const &analysis, QVector<double> const &times, QVector<double> const &value,
                       QVector<double> const &reference, AlignMode mode, int n) {
  if (times.isEmpty()) {
    return NaN;
  }
  QVector<StageAnalysis> const &stages = analysis.run.stages;
  switch (mode) {
    case AlignMode::START:
      return times.first();
    case AlignMode::STAGE:
      return ((n >= 1) && (n <= stages.size())) ? stages[n - 1].segment.start : NaN;
    case AlignMode::SETPOINT_CHANGE:
    case AlignMode::TARGET_CROSSING: {
      QVector<int> const changes = setpointChanges(analysis, 0.05);
      if ((n < 1) || (n > changes.size())) {
        return NaN;
      }
      Segment const &stage = stages[changes[n - 1]].segment;
      if (mode == AlignMode::SETPOINT_CHANGE) {
        return stage.start;
      }
      // first sample from the change on where the error changes sign or reaches zero
      qsizetype const count{std::min({times.size(), value.size(), reference.size()})};
      int             side{0};
      for (qsizetype i{stage.first}; i < count; ++i) {
        double const error{reference[i] - value[i]};
        if (!std::isfinite(error)) {
          continue;
        }
        int const now{error > 0.0 ? 1 : error < 0.0 ? -1 : 0};
        if (side == 0) {
          side = now;
          if (now == 0) {
            return times[i];
          }
        } else if (now != side) {
          return times[i];
        }
      }
      return NaN;
    }
  }
  return NaN;
}

QVector<KpiComparisonRow> compareKpis(QVector<ExperimentAnalysis const *> const &runs) {
  struct Spec {
    char const *key;
    char const *label;
    char const *unit;
    Better      better;
    double (*get)(ExperimentAnalysis const &);
  };
  static Spec const specs[]{
    {"mae", "MAE", "°C", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.overall.mae; }},
    {"rmse", "RMSE", "°C", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.overall.rmse; }},
    {"max_abs_error", "Max |error|", "°C", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.overall.maxAbsError; }},
    {"overshoot", "Overshoot (worst step)", "°C", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.overshoot; }},
    {"undershoot", "Undershoot (worst step)", "°C", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.undershoot; }},
    {"settling", "Settling (last step)", "s", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.settlingTimeS; }},
    {"steady_state_error", "Steady-state error", "°C", Better::NONE,
     [](ExperimentAnalysis const &a) { return a.run.overall.steadyStateError; }},
    {"tail_mae", "Tail MAE", "°C", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.overall.tailMae; }},
    {"tail_std", "Tail std", "°C", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.overall.tailStd; }},
    {"cost", "Control cost", "", Better::LOWER, [](ExperimentAnalysis const &a) { return a.run.cost; }},
    {"steady_fraction", "Time steady", "%", Better::HIGHER,
     [](ExperimentAnalysis const &a) {
       return a.config.durationS > 0.0 ? 100.0 * steadyFraction(a.steady, a.run.stages.isEmpty() ? 0.0 : a.run.stages.first().segment.start,
                                                                a.run.stages.isEmpty() ? a.config.durationS : a.run.stages.last().segment.end)
                                       : NaN;
     }},
    {"saturation", "Output saturated", "%", Better::LOWER,
     [](ExperimentAnalysis const &a) { return 100.0 * a.saturation.fraction; }},
    {"windup", "Integrator windup", "s", Better::LOWER, [](ExperimentAnalysis const &a) { return a.saturation.windupS; }},
    {"share_p", "P share", "%", Better::NONE, [](ExperimentAnalysis const &a) { return 100.0 * a.pid.shareP; }},
    {"share_i", "I share", "%", Better::NONE, [](ExperimentAnalysis const &a) { return 100.0 * a.pid.shareI; }},
    {"share_d", "D share", "%", Better::NONE, [](ExperimentAnalysis const &a) { return 100.0 * a.pid.shareD; }},
    {"anomalies", "Anomalies (warning+)", "", Better::LOWER,
     [](ExperimentAnalysis const &a) { return static_cast<double>(a.anomalyCount(Anomaly::Severity::WARNING)); }},
  };

  QVector<KpiComparisonRow> rows;
  for (Spec const &spec : specs) {
    KpiComparisonRow row;
    row.key    = QString::fromLatin1(spec.key);
    row.label  = QString::fromLatin1(spec.label);
    row.unit   = QString::fromUtf8(spec.unit);
    row.better = spec.better;
    for (ExperimentAnalysis const *run : runs) {
      row.values.push_back(run ? spec.get(*run) : NaN);
    }
    rows.push_back(row);
  }
  return rows;
}

QVector<ConfigComparisonRow> compareConfigs(QVector<RunConfig const *> const &configs) {
  struct Spec {
    char const *label;
    QString (*get)(RunConfig const &);
  };
  static Spec const specs[]{
    {"Duration, s", [](RunConfig const &c) { return text(c.durationS, 6); }},
    {"Sample period, s", [](RunConfig const &c) { return text(c.samplePeriodS, 3); }},
    {"Kp", [](RunConfig const &c) { return text(c.kp); }},
    {"Ki", [](RunConfig const &c) { return text(c.ki); }},
    {"Kd", [](RunConfig const &c) { return text(c.kd); }},
    {"PID gain sets used", [](RunConfig const &c) { return QString::number(c.gainSets); }},
    {"Setpoints, °C",
     [](RunConfig const &c) {
       QStringList levels;
       for (double const setpoint : c.setpoints) {
         levels << text(setpoint);
       }
       return levels.isEmpty() ? QStringLiteral("—") : levels.join(QStringLiteral(" → "));
     }},
    {"Reference range, °C", [](RunConfig const &c) { return QStringLiteral("%1 … %2").arg(text(c.minReference), text(c.maxReference)); }},
    {"Max ramp rate, °C/min", [](RunConfig const &c) { return text(c.maxRampRatePerMin, 3); }},
    {"Stages (step / ramp / hold)",
     [](RunConfig const &c) { return QStringLiteral("%1 (%2 / %3 / %4)").arg(c.stages).arg(c.steps).arg(c.ramps).arg(c.holds); }},
    {"Program intervals", [](RunConfig const &c) { return c.programIntervals > 0 ? QString::number(c.programIntervals) : QStringLiteral("—"); }},
    {"Cycles", [](RunConfig const &c) { return c.cycles > 0 ? QString::number(c.cycles) : QStringLiteral("—"); }},
  };
  QVector<ConfigComparisonRow> rows;
  for (Spec const &spec : specs) {
    ConfigComparisonRow row;
    row.label = QString::fromUtf8(spec.label);
    for (RunConfig const *config : configs) {
      row.values << (config ? spec.get(*config) : QStringLiteral("—"));
    }
    row.differs = std::any_of(row.values.cbegin(), row.values.cend(), [&](QString const &v) { return v != row.values.first(); });
    rows.push_back(row);
  }
  return rows;
}

QVector<StepMatch> matchSteps(QVector<ExperimentAnalysis const *> const &runs) {
  QVector<QVector<int>> steps;
  qsizetype             most{0};
  for (ExperimentAnalysis const *run : runs) {
    QVector<int> indexes;
    if (run) {
      for (qsizetype k{0}; k < run->run.stages.size(); ++k) {
        if (run->run.stages[k].segment.kind == Segment::Kind::SETPOINT) {
          indexes.push_back(static_cast<int>(k));
        }
      }
    }
    most = std::max(most, indexes.size());
    steps.push_back(indexes);
  }
  QVector<StepMatch> matches;
  for (qsizetype k{0}; k < most; ++k) {
    StepMatch match;
    for (QVector<int> const &indexes : std::as_const(steps)) {
      match.stages.push_back(k < indexes.size() ? indexes[k] : -1);
    }
    matches.push_back(match);
  }
  return matches;
}

SignalComparison compareSignals(QVector<double> const &timesA, QVector<double> const &valuesA, double offsetA,
                                QVector<double> const &timesB, QVector<double> const &valuesB, double offsetB) {
  SignalComparison result;
  qsizetype const  nA{std::min(timesA.size(), valuesA.size())};
  qsizetype const  nB{std::min(timesB.size(), valuesB.size())};
  if ((nA == 0) || (nB < 2)) {
    return result;
  }
  double    sumA{0.0}, sumB{0.0}, sumAA{0.0}, sumBB{0.0}, sumAB{0.0}, sumDiff{0.0}, sumDiff2{0.0}, maxDiff{0.0};
  double    first{NaN}, last{NaN};
  qsizetype j{0};
  for (qsizetype i{0}; i < nA; ++i) {
    double const t{timesA[i] - offsetA + offsetB}; // A's sample on B's time axis
    if (!std::isfinite(valuesA[i]) || (t < timesB[0]) || (t > timesB[nB - 1])) {
      continue;
    }
    while ((j + 1 < nB) && (timesB[j + 1] < t)) {
      ++j;
    }
    qsizetype const k{std::min(j + 1, nB - 1)};
    double const    span{timesB[k] - timesB[j]};
    double const    w{span > 0.0 ? (t - timesB[j]) / span : 0.0};
    double const    b{valuesB[j] + w * (valuesB[k] - valuesB[j])};
    if (!std::isfinite(b)) {
      continue;
    }
    double const a{valuesA[i]};
    double const diff{b - a};
    ++result.samples;
    sumA += a;
    sumB += b;
    sumAA += a * a;
    sumBB += b * b;
    sumAB += a * b;
    sumDiff += diff;
    sumDiff2 += diff * diff;
    maxDiff = std::max(maxDiff, std::fabs(diff));
    first   = std::isfinite(first) ? first : timesA[i];
    last    = timesA[i];
  }
  if (result.samples == 0) {
    return result;
  }
  double const n{static_cast<double>(result.samples)};
  result.overlapS   = last - first;
  result.meanDiff   = sumDiff / n;
  result.rmsDiff    = std::sqrt(sumDiff2 / n);
  result.maxAbsDiff = maxDiff;
  double const varA{sumAA - sumA * sumA / n};
  double const varB{sumBB - sumB * sumB / n};
  if ((varA > 0.0) && (varB > 0.0)) {
    result.correlation = (sumAB - sumA * sumB / n) / std::sqrt(varA * varB);
  }
  return result;
}

} // namespace deepvaccontrol::core
