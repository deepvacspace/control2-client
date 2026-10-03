#ifndef CORE_ANALYSIS_EXPERIMENTANALYSIS_H
#define CORE_ANALYSIS_EXPERIMENTANALYSIS_H

#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include "anomalies.h"
#include "controlanalysis.h"
#include "runanalysis.h"
#include "steadystate.h"

namespace deepvaccontrol::core {

// One recorded experiment, channels by state name (temp, temp_ref, temp_u, temp_u_p/i/d,
// temp_kp/ki/kd, interval, cycle_cur, ...). Missing channels just skip the analyses needing them.
struct ExperimentInput {
  QVector<double>                 times; // elapsed s, ascending
  QHash<QString, QVector<double>> channels;
  QVector<LimitRule>              rules; // variable rules of the run (limit violations)
};

struct ExperimentOptions {
  KpiOptions          kpi;
  SegmentationOptions segmentation;
  SteadyStateOptions  steady;
  SaturationOptions   saturation;
  AnomalyOptions      anomaly;
};

// Everything known about one stage beyond its control KPIs (RunAnalysis::stages[i].kpis).
struct StageDetail {
  double          timeToSteadyS{NaN};  // from the stage start until a steady state begins
  double          steadyFraction{NaN}; // share of the stage spent steady
  double          referenceRatePerMin{NaN}; // least-squares trend of the reference (ramps)
  double          valueRatePerMin{NaN};     // ... and of the value
  double          trackingLagS{NaN};        // ramps: mean error / reference rate
  SaturationStats saturation;
  PidContribution pid;
  double          kp{NaN}; // gains in use (median over the stage)
  double          ki{NaN};
  double          kd{NaN};
};

// Configuration of the run, as far as the recorded signals tell it.
struct RunConfig {
  int             samples{0};
  double          durationS{NaN};
  double          samplePeriodS{NaN};
  double          kp{NaN}; // most used gains
  double          ki{NaN};
  double          kd{NaN};
  int             gainSets{0}; // distinct (kp, ki, kd) combinations used
  QVector<double> setpoints;   // levels the reference held, in order
  double          minReference{NaN};
  double          maxReference{NaN};
  double          maxRampRatePerMin{NaN};
  int             stages{0};
  int             steps{0}; // SETPOINT stages
  int             ramps{0};
  int             holds{0};
  int             programIntervals{0}; // highest interval number
  int             cycles{0};           // highest cycle number
  QStringList     channels;
};

struct ExperimentAnalysis {
  bool                    hasControl{false}; // temp and temp_ref present with samples
  RunAnalysis             run;
  QVector<StageDetail>    stageDetails; // parallel to run.stages
  QVector<SteadyInterval> steady;
  SaturationStats         saturation;
  PidContribution         pid;
  QVector<Anomaly>        anomalies; // sorted by start
  RunConfig               config;

  [[nodiscard]] int         anomalyCount(Anomaly::Severity atLeast) const;
  [[nodiscard]] QJsonObject toJson() const; // stored in summary_json (superset of RunAnalysis::toJson)
};

// Bump when toJson() gains content: stored analyses older than this are recomputed.
constexpr int EXPERIMENT_ANALYSIS_VERSION{2};

ExperimentAnalysis analyseExperiment(ExperimentInput const &input, ExperimentOptions const &options = {});

} // namespace deepvaccontrol::core

#endif // CORE_ANALYSIS_EXPERIMENTANALYSIS_H
