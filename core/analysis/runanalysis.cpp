#include "runanalysis.h"

#include <algorithm>

#include <QJsonArray>
#include <cmath>

namespace deepvaccontrol::core {

namespace {
QJsonValue number(double value) {
  return std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null);
}

ControlKpis kpisOver(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference, int first,
                     int last, KpiOptions options) {
  qsizetype const count{last - first + 1};
  QVector<double> const t = times.mid(first, count);
  QVector<double> const v = value.mid(first, count);
  options.tailEndS        = NaN;
  options.initialValue    = NaN; // the stage's own first value and start time
  options.startTimeS      = NaN;
  return computeKpis(t, v, reference.mid(first, count), options);
}
}

RunAnalysis analyseRun(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference,
                       QVector<double> const &interval, QVector<double> const &cycle, KpiOptions const &kpiOptions,
                       SegmentationOptions const &segmentationOptions) {
  RunAnalysis analysis;
  analysis.overall = computeKpis(times, value, reference, kpiOptions);

  QVector<Segment> const segments = segmentExperiment(times, value, reference, interval, cycle, segmentationOptions);
  double                 worstOvershoot{NaN};
  double                 worstUndershoot{NaN};
  for (qsizetype s{0}; s < segments.size(); ++s) {
    Segment const &segment = segments[s];
    int            last{segment.last};
    if ((segment.kind == Segment::Kind::SETPOINT) && (s + 1 < segments.size())
        && (segments[s + 1].kind == Segment::Kind::HOLD)
        && (std::fabs(segments[s + 1].referenceStart - segment.referenceEnd) < segmentationOptions.referenceTolerance)) {
      last = segments[s + 1].last; // step response = approach + hold at the same setpoint
    }
    StageAnalysis stage{segment, kpisOver(times, value, reference, segment.first, last, kpiOptions)};
    if (segment.kind == Segment::Kind::SETPOINT) {
      if (std::isfinite(stage.kpis.undershoot)) {
        worstUndershoot =
          std::isfinite(worstUndershoot) ? std::max(worstUndershoot, stage.kpis.undershoot) : stage.kpis.undershoot;
      }
      if (std::isfinite(stage.kpis.overshoot)) {
        worstOvershoot = std::isfinite(worstOvershoot) ? std::max(worstOvershoot, stage.kpis.overshoot) : stage.kpis.overshoot;
      }
      analysis.settlingTimeS = stage.kpis.settlingTimeS;
    }
    analysis.stages.push_back(stage);
  }

  bool const hasSteps{std::isfinite(worstOvershoot)};
  analysis.overshoot  = hasSteps ? worstOvershoot : analysis.overall.overshoot;
  analysis.undershoot = hasSteps ? worstUndershoot : analysis.overall.undershoot;
  if (!hasSteps) {
    analysis.settlingTimeS = analysis.overall.settlingTimeS;
  }
  ControlKpis forCost = analysis.overall;
  forCost.overshoot   = analysis.overshoot;
  analysis.cost       = (analysis.overall.samples > 0) ? controlCost(forCost) : NaN;
  return analysis;
}

QJsonObject kpisToJson(ControlKpis const &kpis) {
  return QJsonObject{{"samples", kpis.samples},
                     {"mae", number(kpis.mae)},
                     {"rmse", number(kpis.rmse)},
                     {"bias", number(kpis.bias)},
                     {"max_abs_error", number(kpis.maxAbsError)},
                     {"overshoot", number(kpis.overshoot)},
                     {"overshoot_rms", number(kpis.overshootRms)},
                     {"undershoot", number(kpis.undershoot)},
                     {"rise_time_s", number(kpis.riseTimeS)},
                     {"fall_time_s", number(kpis.fallTimeS)},
                     {"time_to_near_s", number(kpis.timeToNearS)},
                     {"settling_time_s", number(kpis.settlingTimeS)},
                     {"steady_state_error", number(kpis.steadyStateError)},
                     {"tail_mae", number(kpis.tailMae)},
                     {"tail_bias", number(kpis.tailBias)},
                     {"tail_std", number(kpis.tailStd)},
                     {"final_error", number(kpis.finalError)},
                     {"min", number(kpis.minValue)},
                     {"max", number(kpis.maxValue)},
                     {"end", number(kpis.endValue)}};
}

bool RunAnalysis::hasRamp() const {
  return std::any_of(stages.cbegin(), stages.cend(), [](StageAnalysis const &s) { return s.segment.kind == Segment::Kind::RAMP; });
}

bool tracksSetpoint(Segment::Kind kind) {
  return (kind == Segment::Kind::RAMP) || (kind == Segment::Kind::HOLD);
}

QJsonObject RunAnalysis::toJson() const {
  QJsonArray stageArray;
  for (StageAnalysis const &stage : stages) {
    QJsonObject item = kpisToJson(stage.kpis);
    item.insert("kind", Segment::kindName(stage.segment.kind));
    item.insert("start_s", stage.segment.start);
    item.insert("end_s", stage.segment.end);
    item.insert("interval", stage.segment.interval);
    item.insert("cycle", stage.segment.cycle);
    item.insert("reference_start", number(stage.segment.referenceStart));
    item.insert("reference_end", number(stage.segment.referenceEnd));
    stageArray.push_back(item);
  }
  QJsonObject run = kpisToJson(overall);
  run.insert("overshoot_worst_step", number(overshoot));
  run.insert("undershoot_worst_step", number(undershoot));
  run.insert("settling_time_last_step_s", number(settlingTimeS));
  run.insert("cost", number(cost));
  return QJsonObject{{"kpis", run}, {"stages", stageArray}};
}

}
