#ifndef CORE_ANALYSIS_ANOMALIES_H
#define CORE_ANALYSIS_ANOMALIES_H

#include <QString>
#include <QVector>

#include "controlanalysis.h"
#include "kpis.h"
#include "runanalysis.h"

namespace deepvaccontrol::core {

struct Anomaly {
  enum class Kind : uint8_t {
    LIMIT,       // a channel outside a variable rule's band
    SPIKE,       // isolated jump that reverts (statistical outlier of the sample-to-sample change)
    FLATLINE,    // a measured channel stuck at one value (sensor / communication)
    GAP,         // missing data
    OSCILLATION, // the value keeps crossing the reference
    NOT_REACHED, // a setpoint the value never settled at
    SATURATION   // controller output at its limit for a long time
  };
  enum class Severity : uint8_t {
    INFO,
    WARNING,
    CRITICAL
  };

  Kind     kind{Kind::LIMIT};
  Severity severity{Severity::WARNING};
  QString  channel;
  double   start{0.0}; // elapsed s
  double   end{0.0};
  double   value{NaN}; // worst value / excursion / statistic, depends on the kind
  QString  detail;     // human readable (English, the UI translates the kind)

  static QString kindName(Kind kind);         // "limit", "spike", ...
  static QString severityName(Severity severity); // "info", "warning", "critical"
};

// variable rule: acceptable band, NaN = unbounded side
struct LimitRule {
  QString channel;
  double  lo{NaN};
  double  hi{NaN};
  QString label;
};

struct AnomalyOptions {
  double spikeZ{8.0};              // robust z-score (median / MAD) of the sample-to-sample change
  int    spikeRevertSamples{3};    // ... that is undone within this many samples
  double spikeMinJump{0.0};        // ... and at least this big (channel units)
  double spikeMinJumpOfSpan{0.02}; // ... and at least this part of the channel's 1-99 % span
  double flatlineS{900.0};         // a measured channel that doesn't change at all for this long
  double gapFactor{5.0};           // time step > factor x the typical sample period ...
  double minGapS{10.0};            // ... and at least this long
  double oscillationWindowS{600.0};
  int    oscillationCrossings{6};  // reference crossings within the window
  double crossingDeadband{0.2};    // |error| below this doesn't count as a side
  double nearBand{2.0};            // a step response that never gets this close is critical
  double saturationS{300.0};       // continuous output saturation longer than this
};

QVector<Anomaly> detectLimitViolations(QVector<double> const &times, QVector<double> const &values, LimitRule const &rule);
QVector<Anomaly> detectSpikes(QVector<double> const &times, QVector<double> const &values, QString const &channel,
                              AnomalyOptions const &options = {});
QVector<Anomaly> detectFlatlines(QVector<double> const &times, QVector<double> const &values, QString const &channel,
                                 AnomalyOptions const &options = {});
// time gaps between samples and stretches without a value of `channel`
QVector<Anomaly> detectGaps(QVector<double> const &times, QVector<double> const &values, QString const &channel,
                            AnomalyOptions const &options = {});
QVector<Anomaly> detectOscillation(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference,
                                   AnomalyOptions const &options = {});
// step responses that never settled (from analyseRun's stages)
QVector<Anomaly> detectUnreachedSetpoints(RunAnalysis const &analysis, AnomalyOptions const &options = {});
QVector<Anomaly> detectLongSaturation(QVector<SaturationEpisode> const &episodes, AnomalyOptions const &options = {});

} // namespace deepvaccontrol::core

#endif // CORE_ANALYSIS_ANOMALIES_H
