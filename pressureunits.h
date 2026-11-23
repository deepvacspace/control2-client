#ifndef PRESSUREUNITS_H
#define PRESSUREUNITS_H

#include <QString>
#include <cmath>
#include <limits>
#include <utility>

#include "utils.h"

// Pressure is shown in Torr everywhere. The chamber reports it in the unit of its channel label ("Pressure, mbar");
// without one it is mmHg (1 mmHg = 1 Torr).
namespace deepvaccontrol::pressure {

// the unit in a channel label, lower case; empty: none
inline QString unitOf(QString const &label) {
  qsizetype const comma = label.lastIndexOf(QLatin1Char(','));
  return (comma >= 0) ? label.mid(comma + 1).trimmed().toLower() : QString{};
}

// the chamber reports Torr (or mmHg): its values need no conversion
inline bool isTorr(QString const &label) {
  QString const unit = unitOf(label);
  return unit.isEmpty() || (unit == QLatin1String("torr")) || (unit == QLatin1String("mmhg"));
}

// a reading in the label's unit, in Torr (NaN for a missing or absurd one)
inline double toTorr(double value, QString const &label) {
  if (!std::isfinite(value) || (std::fabs(value) > 1e9)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  QString const unit = unitOf(label);
  if ((unit == QLatin1String("mbar")) || (unit == QLatin1String("hpa"))) {
    return value * 0.750062;
  }
  if (unit == QLatin1String("pa")) {
    return value * 0.00750062;
  }
  if (unit == QLatin1String("kpa")) {
    return value * 7.50062;
  }
  if (unit == QLatin1String("bar")) {
    return value * 750.062;
  }
  if (unit == QLatin1String("atm")) {
    return value * 760.0;
  }
  return value; // Torr, mmHg
}

// the chamber pressure and its setpoint: drawn in Torr on a logarithmic axis
inline bool isPressureChannel(QString const &name) {
  return (name == QLatin1String("pres")) || (name == QLatin1String("pres_ref"));
}

// "Pressure, mbar" -> "Pressure, Torr": the label of a line drawn in Torr
inline QString torrLabel(QString const &label) {
  qsizetype const comma = label.lastIndexOf(QLatin1Char(','));
  return QStringLiteral("%1, Torr").arg((comma >= 0) ? label.left(comma).trimmed() : label.trimmed());
}

// a logarithmic axis range around [lo, hi] in whole decades, at least half a decade clear of the values (so at
// least two decades); a hair wider, so its end decades keep their labels; NaN when there are no values
inline std::pair<double, double> decadeRange(double lo, double hi) {
  if (!std::isfinite(lo) || !std::isfinite(hi) || (lo <= 0.0) || (hi <= 0.0)) {
    return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
  }
  double const low{std::floor(std::log10(lo) - 0.5)};
  double const high{std::ceil(std::log10(hi) + 0.5)};
  return {std::pow(10.0, low) * 0.999, std::pow(10.0, high) * 1.001};
}

// a value in Torr as text: scientific below 1 Torr (vacuum), a dash when there is none
inline QString text(double torr) {
  if (!std::isfinite(torr)) {
    return QStringLiteral("—");
  }
  if ((std::fabs(torr) < 1.0) && !qFuzzyIsNull(torr)) {
    return utils::formatSci10(torr);
  }
  return QString::number(torr, 'f', std::fabs(torr) >= 10.0 ? 0 : 1).replace(QLatin1Char('-'), QChar(0x2212));
}

}

#endif // PRESSUREUNITS_H
