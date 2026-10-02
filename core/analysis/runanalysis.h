#ifndef CORE_ANALYSIS_RUNANALYSIS_H
#define CORE_ANALYSIS_RUNANALYSIS_H

#include <QJsonObject>
#include <QVector>

#include "kpis.h"
#include "segmentation.h"

namespace deepvaccontrol::core {

struct StageAnalysis {
  Segment     segment;
  ControlKpis kpis; // SETPOINT stages: the whole step response (approach + the hold that follows)
};

struct RunAnalysis {
  ControlKpis            overall;          // whole run against the live reference
  double                 overshoot{NaN};   // worst step response (whole run when there is no step)
  double                 undershoot{NaN};  // worst step response (whole run when there is no step)
  double                 settlingTimeS{NaN}; // settling of the last step response (from its start)
  double                 cost{NaN};
  QVector<StageAnalysis> stages;

  [[nodiscard]] QJsonObject toJson() const; // stored in the experiment database (summary_json)
};

// times: elapsed s; interval / cycle may be empty
RunAnalysis analyseRun(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference,
                       QVector<double> const &interval, QVector<double> const &cycle, KpiOptions const &kpiOptions = {},
                       SegmentationOptions const &segmentationOptions = {});

QJsonObject kpisToJson(ControlKpis const &kpis);

} // namespace deepvaccontrol::core

#endif // CORE_ANALYSIS_RUNANALYSIS_H
