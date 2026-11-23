#include "experimentinput.h"

#include <QObject>
#include <cmath>

#include "utils.h"

namespace deepvaccontrol {

core::ExperimentInput experimentInput(ExperimentData const &data, QStringList const &channelNames,
                                      QVector<AnalysisNotesStore::Rule> const &rules) {
  core::ExperimentInput input;
  double const          start{data.times.isEmpty() ? 0.0 : data.times.first()};
  input.times.reserve(data.times.size());
  for (double const time : data.times) {
    input.times.push_back(time - start);
  }
  for (qsizetype c{0}; (c < channelNames.size()) && (c < data.states.size()); ++c) {
    if (!channelNames[c].isEmpty()) {
      input.channels.insert(channelNames[c], data.states[c]);
    }
  }
  for (AnalysisNotesStore::Rule const &rule : rules) {
    input.rules.push_back({rule.channel, rule.lo, rule.hi, rule.label});
  }
  return input;
}

namespace analysistext {

QString stageName(core::Segment::Kind kind) {
  switch (kind) {
    case core::Segment::Kind::RAMP:
      return QObject::tr("Ramp");
    case core::Segment::Kind::SETPOINT:
      return QObject::tr("Setpoint");
    case core::Segment::Kind::HOLD:
      return QObject::tr("Hold");
    default:
      return QObject::tr("Unknown");
  }
}

QColor stageColor(core::Segment::Kind kind) {
  switch (kind) {
    case core::Segment::Kind::RAMP:
      return QColor("#f2a60c");
    case core::Segment::Kind::SETPOINT:
      return QColor("#2760e6");
    case core::Segment::Kind::HOLD:
      return QColor("#19b26b");
    default:
      return QColor("#5c6472");
  }
}

QString anomalyName(core::Anomaly::Kind kind) {
  switch (kind) {
    case core::Anomaly::Kind::LIMIT:
      return QObject::tr("Limit");
    case core::Anomaly::Kind::SPIKE:
      return QObject::tr("Spike");
    case core::Anomaly::Kind::FLATLINE:
      return QObject::tr("Flat signal");
    case core::Anomaly::Kind::GAP:
      return QObject::tr("Missing data");
    case core::Anomaly::Kind::OSCILLATION:
      return QObject::tr("Oscillation");
    case core::Anomaly::Kind::NOT_REACHED:
      return QObject::tr("Setpoint not reached");
    default:
      return QObject::tr("Saturation");
  }
}

QString severityName(core::Anomaly::Severity severity) {
  switch (severity) {
    case core::Anomaly::Severity::INFO:
      return QObject::tr("Info");
    case core::Anomaly::Severity::WARNING:
      return QObject::tr("Warning");
    default:
      return QObject::tr("Critical");
  }
}

QColor severityColor(core::Anomaly::Severity severity) {
  switch (severity) {
    case core::Anomaly::Severity::INFO:
      return QColor("#5c6472");
    case core::Anomaly::Severity::WARNING:
      return QColor("#f2a60c");
    default:
      return QColor("#d4122e");
  }
}

QString number(double value, int precision) {
  if (!std::isfinite(value)) {
    return QStringLiteral("—");
  }
  double const absValue{std::fabs(value)};
  if ((absValue != 0.0) && ((absValue < 1e-3) || (absValue >= 1e6))) {
    return utils::formatScientific(value, 2, false);
  }
  return QString::number(value, 'f', absValue >= 100 ? 1 : precision > 2 ? 3 : precision);
}

QString seconds(double value) {
  return std::isfinite(value) ? ExperimentLibrary::formatDuration(value) : QStringLiteral("—");
}

QString percent(double fraction) {
  return std::isfinite(fraction) ? QStringLiteral("%1 %").arg(100.0 * fraction, 0, 'f', 1) : QStringLiteral("—");
}

QString pidShares(core::PidContribution const &pid) {
  if (!std::isfinite(pid.shareP)) {
    return QStringLiteral("—");
  }
  return QStringLiteral("%1 / %2 / %3 %")
    .arg(100.0 * pid.shareP, 0, 'f', 0)
    .arg(100.0 * pid.shareI, 0, 'f', 0)
    .arg(100.0 * pid.shareD, 0, 'f', 0);
}

}
}
