#ifndef CORE_ANALYSIS_COMPARISON_H
#define CORE_ANALYSIS_COMPARISON_H

#include <QString>
#include <QStringList>
#include <QVector>

#include "experimentanalysis.h"

namespace deepvaccontrol::core {

// What the runs of a comparison are lined up on.
enum class AlignMode : uint8_t {
  START,           // run start
  SETPOINT_CHANGE, // n-th change of the reference (step or ramp start)
  TARGET_CROSSING, // first time the value reaches / crosses the reference after the n-th change
  STAGE            // start of stage n
};

// Elapsed time of the anchor in one run (n is 1-based); NaN when the run has no such anchor.
double alignmentAnchor(ExperimentAnalysis const &analysis, QVector<double> const &times, QVector<double> const &value,
                       QVector<double> const &reference, AlignMode mode, int n);

enum class Better : uint8_t {
  LOWER,
  HIGHER,
  NONE
};

struct KpiComparisonRow {
  QString         key;   // stable id
  QString         label; // English
  QString         unit;  // "", "s", "%", "°C"
  Better          better{Better::LOWER};
  QVector<double> values; // one per run, NaN when not available
};

// The headline KPIs of every run side by side (first run = reference of the comparison).
QVector<KpiComparisonRow> compareKpis(QVector<ExperimentAnalysis const *> const &runs);

struct ConfigComparisonRow {
  QString     label;
  QStringList values; // one per run
  bool        differs{false};
};

QVector<ConfigComparisonRow> compareConfigs(QVector<RunConfig const *> const &configs);

// Step responses of every run paired up: k-th step of each run (index into run.stages, -1 when missing).
struct StepMatch {
  QVector<int> stages; // one per run
};
QVector<StepMatch> matchSteps(QVector<ExperimentAnalysis const *> const &runs);

// Difference of signal B against signal A after shifting both by their alignment offsets
// (B is interpolated at A's samples, only where they overlap).
struct SignalComparison {
  int    samples{0};
  double overlapS{NaN};
  double meanDiff{NaN}; // mean(B - A)
  double rmsDiff{NaN};
  double maxAbsDiff{NaN};
  double correlation{NaN}; // Pearson
};

SignalComparison compareSignals(QVector<double> const &timesA, QVector<double> const &valuesA, double offsetA,
                                QVector<double> const &timesB, QVector<double> const &valuesB, double offsetB);

} // namespace deepvaccontrol::core

#endif // CORE_ANALYSIS_COMPARISON_H
