#ifndef MODELENVELOPE_H
#define MODELENVELOPE_H

#include <QString>
#include <limits>

namespace deepvaccontrol::ai {

// Measured chamber temperature may leave the trained envelope by this much
// (start at room temperature, small overshoot) before the model is
// considered to be extrapolating.
constexpr double ENVELOPE_TEMP_MARGIN_C{2.0};

// The operating range a model was trained on. Outside of it its decisions
// are extrapolation and must not reach the chamber.
struct ModelEnvelope {
  double tempMin{-5.0};
  double tempMax{26.0};
  double maxDurationS{std::numeric_limits<double>::infinity()};
  double kpMin{1.0};
  double kpMax{50.0};
  double kiMin{1.0};
  double kiMax{1000.0};
  double kdMin{1.0};
  double kdMax{20.0};

  // Empty when the scenario is inside the envelope, otherwise why not.
  // The target must lie inside the trained range, the measured start
  // temperature inside it +/- ENVELOPE_TEMP_MARGIN_C.
  QString checkScenario(double startTemp, double targetTemp, double durationS) const;
  bool    containsTemp(double temp, double margin = ENVELOPE_TEMP_MARGIN_C) const;
  bool    containsPid(double kp, double ki, double kd) const;
  QString describe() const;
};

}

#endif // MODELENVELOPE_H
