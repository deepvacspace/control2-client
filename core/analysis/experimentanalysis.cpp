#include "experimentanalysis.h"

#include <QJsonArray>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <tuple>

#include "seriesstats.h"

namespace deepvaccontrol::core {

namespace {
QJsonValue number(double value) {
  return std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null);
}

double median(QVector<double> const &values, int first, int last) {
  QVector<double> valid;
  for (int i{std::max(first, 0)}; (i <= last) && (i < values.size()); ++i) {
    if (std::isfinite(values[i])) {
      valid.push_back(values[i]);
    }
  }
  return valid.isEmpty() ? NaN : percentile(valid, 0.5);
}

double highest(QVector<double> const &values) {
  double result{NaN};
  for (double const value : values) {
    if (std::isfinite(value)) {
      result = std::isfinite(result) ? std::max(result, value) : value;
    }
  }
  return result;
}

// channels whose values are measurements (spikes / flatlines make sense there)
bool isMeasured(QString const &name) {
  return (name == QLatin1String("temp")) || (name == QLatin1String("temp_raw")) || name.startsWith(QLatin1String("pres_"));
}

QJsonObject saturationJson(SaturationStats const &s) {
  return QJsonObject{{"fraction", number(s.fraction)},     {"fraction_high", number(s.fractionHigh)},
                     {"fraction_low", number(s.fractionLow)}, {"longest_s", number(s.longestS)},
                     {"episodes", s.episodes},              {"windup_s", number(s.windupS)},
                     {"u_mean", number(s.uMean)},           {"u_min", number(s.uMin)},
                     {"u_max", number(s.uMax)}};
}

QJsonObject pidJson(PidContribution const &p) {
  return QJsonObject{{"share_p", number(p.shareP)},     {"share_i", number(p.shareI)},     {"share_d", number(p.shareD)},
                     {"mean_abs_p", number(p.meanAbsP)}, {"mean_abs_i", number(p.meanAbsI)}, {"mean_abs_d", number(p.meanAbsD)},
                     {"dominant", p.dominant()}};
}
}

int ExperimentAnalysis::anomalyCount(Anomaly::Severity atLeast) const {
  return static_cast<int>(std::count_if(anomalies.cbegin(), anomalies.cend(),
                                        [atLeast](Anomaly const &a) { return a.severity >= atLeast; }));
}

ExperimentAnalysis analyseExperiment(ExperimentInput const &input, ExperimentOptions const &options) {
  ExperimentAnalysis result;
  QVector<double> const &times = input.times;
  qsizetype const        n{times.size()};
  auto                   channel = [&](char const *name) {
    QVector<double> const values = input.channels.value(QString::fromLatin1(name));
    return (values.size() >= n) ? values : QVector<double>{};
  };
  QVector<double> const value     = channel("temp");
  QVector<double> const reference = channel("temp_ref");
  QVector<double> const u         = channel("temp_u");
  QVector<double> const uP        = channel("temp_u_p");
  QVector<double> const uI        = channel("temp_u_i");
  QVector<double> const uD        = channel("temp_u_d");
  QVector<double> const kp        = channel("temp_kp");
  QVector<double> const ki        = channel("temp_ki");
  QVector<double> const kd        = channel("temp_kd");
  QVector<double> const interval  = channel("interval");
  QVector<double> const cycle     = channel("cycle_cur");

  // ---- control KPIs, stages, steady state
  if (!value.isEmpty() && !reference.isEmpty()) {
    result.run        = analyseRun(times, value, reference, interval, cycle, options.kpi, options.segmentation);
    result.hasControl = result.run.overall.samples > 0;
  }
  if (!value.isEmpty()) {
    result.steady = detectSteadyStates(times, value, reference, options.steady);
  }

  // ---- whole-run control behaviour
  QVector<SaturationEpisode> episodes;
  if (n > 0) {
    if (!u.isEmpty()) {
      result.saturation = analyseSaturation(times, u, uI, 0, static_cast<int>(n - 1), options.saturation, &episodes);
    }
    if (!uP.isEmpty() && !uI.isEmpty() && !uD.isEmpty()) {
      result.pid = analysePidContribution(uP, uI, uD, 0, static_cast<int>(n - 1));
    }
  }

  // ---- per stage
  for (StageAnalysis const &stage : std::as_const(result.run.stages)) {
    Segment const &s = stage.segment;
    StageDetail    detail;
    double const   steadyAt{firstSteadyTime(result.steady, s.start, s.end)};
    detail.timeToSteadyS  = std::isfinite(steadyAt) ? steadyAt - s.start : NaN;
    detail.steadyFraction = steadyFraction(result.steady, s.start, s.end);
    if (s.kind == Segment::Kind::RAMP) {
      SeriesStats const referenceTrend = computeStats(times, reference, s.start, s.end);
      SeriesStats const valueTrend     = computeStats(times, value, s.start, s.end);
      detail.referenceRatePerMin       = referenceTrend.slopePerS * 60.0;
      detail.valueRatePerMin           = valueTrend.slopePerS * 60.0;
      if (std::fabs(referenceTrend.slopePerS) > 1e-9) {
        detail.trackingLagS = stage.kpis.bias / referenceTrend.slopePerS;
      }
    }
    if (!u.isEmpty()) {
      detail.saturation = analyseSaturation(times, u, uI, s.first, s.last, options.saturation);
    }
    if (!uP.isEmpty() && !uI.isEmpty() && !uD.isEmpty()) {
      detail.pid = analysePidContribution(uP, uI, uD, s.first, s.last);
    }
    detail.kp = kp.isEmpty() ? NaN : median(kp, s.first, s.last);
    detail.ki = ki.isEmpty() ? NaN : median(ki, s.first, s.last);
    detail.kd = kd.isEmpty() ? NaN : median(kd, s.first, s.last);
    result.stageDetails.push_back(detail);
  }

  // ---- configuration
  RunConfig &config = result.config;
  config.samples    = static_cast<int>(n);
  if (n > 1) {
    config.durationS = times.last() - times.first();
    QVector<double> steps;
    for (qsizetype i{1}; i < n; ++i) {
      steps.push_back(times[i] - times[i - 1]);
    }
    config.samplePeriodS = percentile(steps, 0.5);
  }
  config.channels = input.channels.keys();
  for (char const *alias : {"kp", "ki", "kd"}) { // Insight-compatible copies of temp_kp / temp_ki / temp_kd
    if (input.channels.contains(QStringLiteral("temp_") + QLatin1String(alias))) {
      config.channels.removeAll(QLatin1String(alias));
    }
  }
  config.channels.sort();
  if (!kp.isEmpty() && !ki.isEmpty() && !kd.isEmpty()) {
    QMap<std::tuple<double, double, double>, int> used;
    for (qsizetype i{0}; i < n; ++i) {
      if (std::isfinite(kp[i]) && std::isfinite(ki[i]) && std::isfinite(kd[i])) {
        ++used[{kp[i], ki[i], kd[i]}];
      }
    }
    config.gainSets = static_cast<int>(used.size());
    int best{0};
    for (auto it = used.cbegin(); it != used.cend(); ++it) {
      if (it.value() > best) {
        best                                  = it.value();
        std::tie(config.kp, config.ki, config.kd) = it.key();
      }
    }
  }
  if (!reference.isEmpty()) {
    SeriesStats const referenceStats = computeStats(times, reference);
    config.minReference              = referenceStats.min;
    config.maxReference              = referenceStats.max;
  }
  for (qsizetype k{0}; k < result.run.stages.size(); ++k) {
    Segment const &s = result.run.stages[k].segment;
    ++config.stages;
    if (s.kind == Segment::Kind::SETPOINT) {
      ++config.steps;
    } else if (s.kind == Segment::Kind::RAMP) {
      ++config.ramps;
      double const rate{std::fabs(result.stageDetails[k].referenceRatePerMin)};
      if (std::isfinite(rate)) {
        config.maxRampRatePerMin = std::isfinite(config.maxRampRatePerMin) ? std::max(config.maxRampRatePerMin, rate) : rate;
      }
    } else if (s.kind == Segment::Kind::HOLD) {
      ++config.holds;
    }
    // a level the reference sat at (SETPOINT followed by its HOLD counts once)
    if ((s.kind != Segment::Kind::RAMP) && std::isfinite(s.referenceEnd)
        && (config.setpoints.isEmpty() || std::fabs(config.setpoints.last() - s.referenceEnd) >= options.segmentation.referenceTolerance)) {
      config.setpoints.push_back(s.referenceEnd);
    }
  }
  config.programIntervals = interval.isEmpty() ? 0 : std::max(0, static_cast<int>(highest(interval)));
  config.cycles           = cycle.isEmpty() ? 0 : std::max(0, static_cast<int>(highest(cycle)));

  // ---- anomalies
  QVector<Anomaly> &anomalies = result.anomalies;
  for (LimitRule const &rule : input.rules) {
    QVector<double> const values = input.channels.value(rule.channel);
    if (values.size() >= n) {
      anomalies += detectLimitViolations(times, values, rule);
    }
  }
  for (auto it = input.channels.cbegin(); it != input.channels.cend(); ++it) {
    if (isMeasured(it.key()) && (it.value().size() >= n)) {
      anomalies += detectSpikes(times, it.value(), it.key(), options.anomaly);
      anomalies += detectFlatlines(times, it.value(), it.key(), options.anomaly);
    }
  }
  anomalies += detectGaps(times, value, QStringLiteral("temp"), options.anomaly);
  if (result.hasControl) {
    anomalies += detectOscillation(times, value, reference, options.anomaly);
    anomalies += detectUnreachedSetpoints(result.run, options.anomaly);
  }
  anomalies += detectLongSaturation(episodes, options.anomaly);
  std::stable_sort(anomalies.begin(), anomalies.end(), [](Anomaly const &a, Anomaly const &b) { return a.start < b.start; });
  return result;
}

QJsonObject ExperimentAnalysis::toJson() const {
  QJsonObject json = run.toJson();
  json.insert("analysis_version", EXPERIMENT_ANALYSIS_VERSION);

  QJsonArray stages = json.value("stages").toArray();
  for (qsizetype k{0}; (k < stages.size()) && (k < stageDetails.size()); ++k) {
    StageDetail const &d    = stageDetails[k];
    QJsonObject        item = stages[k].toObject();
    item.insert("time_to_steady_s", number(d.timeToSteadyS));
    item.insert("steady_fraction", number(d.steadyFraction));
    item.insert("reference_rate_per_min", number(d.referenceRatePerMin));
    item.insert("value_rate_per_min", number(d.valueRatePerMin));
    item.insert("tracking_lag_s", number(d.trackingLagS));
    item.insert("saturation", saturationJson(d.saturation));
    item.insert("pid", pidJson(d.pid));
    item.insert("kp", number(d.kp));
    item.insert("ki", number(d.ki));
    item.insert("kd", number(d.kd));
    stages[k] = item;
  }
  json.insert("stages", stages);

  QJsonArray steadyArray;
  for (SteadyInterval const &s : steady) {
    steadyArray.push_back(QJsonObject{{"start_s", s.start}, {"end_s", s.end}, {"mean", number(s.mean)}, {"std", number(s.std)},
                                      {"mean_error", number(s.meanError)}});
  }
  json.insert("steady_states", steadyArray);
  json.insert("saturation", saturationJson(saturation));
  json.insert("pid", pidJson(pid));

  QJsonArray anomalyArray;
  for (Anomaly const &a : anomalies) {
    anomalyArray.push_back(QJsonObject{{"kind", Anomaly::kindName(a.kind)},
                                       {"severity", Anomaly::severityName(a.severity)},
                                       {"channel", a.channel},
                                       {"start_s", a.start},
                                       {"end_s", a.end},
                                       {"value", number(a.value)},
                                       {"detail", a.detail}});
  }
  json.insert("anomalies", anomalyArray);

  QJsonArray setpoints;
  for (double const setpoint : config.setpoints) {
    setpoints.push_back(setpoint);
  }
  json.insert("config", QJsonObject{{"samples", config.samples},
                                    {"duration_s", number(config.durationS)},
                                    {"sample_period_s", number(config.samplePeriodS)},
                                    {"kp", number(config.kp)},
                                    {"ki", number(config.ki)},
                                    {"kd", number(config.kd)},
                                    {"gain_sets", config.gainSets},
                                    {"setpoints", setpoints},
                                    {"min_reference", number(config.minReference)},
                                    {"max_reference", number(config.maxReference)},
                                    {"max_ramp_rate_per_min", number(config.maxRampRatePerMin)},
                                    {"stages", config.stages},
                                    {"steps", config.steps},
                                    {"ramps", config.ramps},
                                    {"holds", config.holds},
                                    {"program_intervals", config.programIntervals},
                                    {"cycles", config.cycles}});
  return json;
}

}
