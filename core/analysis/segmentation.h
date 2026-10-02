#ifndef CORE_ANALYSIS_SEGMENTATION_H
#define CORE_ANALYSIS_SEGMENTATION_H

#include <QString>
#include <QVector>

#include "kpis.h"

namespace deepvaccontrol::core {

/**
 * Stage of an experiment:
 * - RAMP: the reference moves gradually;
 * - SETPOINT: the reference is constant and the value is still moving towards it (after a step);
 * - HOLD: the reference is constant and the value has reached it (soak / steady state);
 * - UNKNOWN: no reference recorded.
 */
struct Segment {
  enum class Kind : uint8_t {
    RAMP,
    SETPOINT,
    HOLD,
    UNKNOWN
  };

  Kind   kind{Kind::UNKNOWN};
  int    first{0}; // sample indexes, inclusive
  int    last{0};
  double start{0.0}; // times of first / last
  double end{0.0};
  int    interval{-1}; // PLC program interval (1-based), -1 when not recorded
  int    cycle{-1};    // PLC program cycle, -1 when not recorded
  double referenceStart{NaN};
  double referenceEnd{NaN};

  [[nodiscard]] double duration() const {
    return end - start;
  }
  static QString kindName(Kind kind); // "ramp", "setpoint", "hold", "unknown"
};

struct SegmentationOptions {
  double referenceTolerance{0.05}; // reference changes smaller than this are noise
  double settleBand{0.5};          // SETPOINT ends / HOLD starts when |reference - value| first gets this small
  double minDurationS{30.0};       // shorter stages are merged into the previous one
};

/**
 * Splits an experiment into stages. Boundaries come from the recorded PLC program position
 * (interval / cycle changes) when available, and from the reference itself (steps, start / end of ramps).
 * Any of interval, cycle, reference, value may be empty.
 */
QVector<Segment> segmentExperiment(QVector<double> const &times, QVector<double> const &value, QVector<double> const &reference,
                                   QVector<double> const &interval, QVector<double> const &cycle,
                                   SegmentationOptions const &options = {});

} // namespace deepvaccontrol::core

#endif // CORE_ANALYSIS_SEGMENTATION_H
