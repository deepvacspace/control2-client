#include "runrecord.h"

#include <QDateTime>
#include <QJsonArray>
#include <QObject>
#include <cmath>

#include "constants.h"
#include "experimentlibrary.h"

namespace deepvaccontrol::runrecord {

namespace {
QString number(double value) {
  return std::isfinite(value) ? QString::number(value, 'g', 6) : QStringLiteral("—");
}

bool bit(double flags, int index) {
  return std::isfinite(flags) && ((static_cast<quint64>(flags) >> index) & 1U);
}

QString kindName(QString const &kind) {
  static QHash<QString, QString> const names{{"manual_setpoint", QObject::tr("manual setpoint")},
                                             {"program", QObject::tr("program")},
                                             {"test_profile", QObject::tr("test profile")},
                                             {"ai_advisor", QObject::tr("AI Advisor (online)")},
                                             {"offline_advisor", QObject::tr("AI Advisor (offline tuning)")},
                                             {"external", QObject::tr("started outside this application")},
                                             {"job", QObject::tr("job")}};
  return names.value(kind, kind);
}
}

QString jobText(QJsonObject const &configuration) {
  QJsonObject const job = configuration.value("job").toObject();
  if (job.isEmpty()) {
    return QObject::tr("not recorded");
  }
  QString text = job.value("label").toString();
  if (!job.value("test_profile").toString().isEmpty() && !text.contains(job.value("test_profile").toString())) {
    text += QStringLiteral(" \"%1\"").arg(job.value("test_profile").toString());
  }
  return QStringLiteral("%1 (%2)").arg(text.isEmpty() ? QObject::tr("job") : text, kindName(job.value("kind").toString()));
}

QString capturedText(QJsonObject const &configuration) {
  if (!configuration.value("captured_at_execution").toBool(false)) {
    return QObject::tr("not captured at execution (experiment saved later, imported or recorded by an older version)");
  }
  QDateTime const at = QDateTime::fromString(configuration.value("captured_at").toString(), Qt::ISODateWithMs);
  QString         text = QObject::tr("captured when the job was sent, %1").arg(at.toLocalTime().toString("yyyy-MM-dd HH:mm:ss"));
  if (configuration.contains("note")) {
    text += QStringLiteral(" (%1)").arg(configuration.value("note").toString());
  }
  return text;
}

QStringList programColumns() {
  return {QObject::tr("#"), QObject::tr("Setpoint"), QObject::tr("Duration"), QObject::tr("Mode")};
}

QVector<QStringList> programRows(QJsonObject const &configuration) {
  QVector<QStringList> rows;
  QJsonArray const     intervals = configuration.value("job").toObject().value("program").toObject().value("intervals").toArray();
  for (qsizetype i{0}; i < intervals.size(); ++i) {
    QJsonObject const interval = intervals[i].toObject();
    double const      flags    = interval.value("flags").toDouble(qQNaN());
    double const      duration = interval.value("duration").toDouble(qQNaN());
    QStringList       mode;
    if (bit(flags, deepvaccontrol::RAMP_BIT)) {
      mode << QObject::tr("ramp");
    }
    if (bit(flags, deepvaccontrol::AWAIT_BIT)) {
      mode << QObject::tr("wait for setpoint");
    }
    // an indefinite hold is sent as a very long duration
    QString const durationText = (duration >= 99.0 * 3600.0) ? QObject::tr("until stopped") : ExperimentLibrary::formatDuration(duration);
    rows.push_back({QString::number(i + 1),
                    interval.contains("temp") ? QStringLiteral("%1 °C").arg(number(interval.value("temp").toDouble(qQNaN()))) : QStringLiteral("—"),
                    durationText, mode.isEmpty() ? QObject::tr("step / hold") : mode.join(QStringLiteral(", "))});
  }
  return rows;
}

QString programSummary(QJsonObject const &configuration) {
  QVector<QStringList> const rows = programRows(configuration);
  if (rows.isEmpty()) {
    return QObject::tr("not recorded");
  }
  QStringList parts;
  for (QStringList const &row : rows) {
    parts << QStringLiteral("%1 %2 %3").arg(row[1], row[3] == QObject::tr("step / hold") ? QString() : row[3], row[2]).simplified();
  }
  QJsonObject const cycle = configuration.value("job").toObject().value("program").toObject().value("cycle").toObject();
  QString           text  = QObject::tr("%n interval(s): ", nullptr, static_cast<int>(rows.size())) + parts.join(QStringLiteral(" → "));
  if (cycle.value("count").toInt() > 1) {
    text += QObject::tr(" (intervals %1–%2 × %3)").arg(cycle.value("start").toInt() + 1).arg(cycle.value("end").toInt() + 1).arg(cycle.value("count").toInt());
  }
  return text;
}

QStringList pidColumns() {
  return {QObject::tr("Loop"), QObject::tr("Band"), QObject::tr("Range"), QObject::tr("Kp"), QObject::tr("Ki"), QObject::tr("Kd")};
}

QVector<QStringList> pidRows(QJsonObject const &configuration) {
  QVector<QStringList> rows;
  for (QJsonValue const &loopValue : configuration.value("plc_settings").toObject().value("pids").toArray()) {
    QJsonObject const loop   = loopValue.toObject();
    int const         points = loop.value("points").toInt();
    double const      lo     = loop.value("min").toDouble();
    double const      hi     = loop.value("max").toDouble();
    double const      width  = points > 0 ? (hi - lo) / points : 0.0;
    for (int band{0}; band < points; ++band) {
      rows.push_back({loop.value("label").toString(loop.value("name").toString()), QString::number(band),
                      QStringLiteral("%1 … %2").arg(number(lo + band * width), number(lo + (band + 1) * width)),
                      number(loop.value("p").toArray().at(band).toDouble(qQNaN())), number(loop.value("i").toArray().at(band).toDouble(qQNaN())),
                      number(loop.value("d").toArray().at(band).toDouble(qQNaN()))});
    }
  }
  return rows;
}

QString pidSummary(QJsonObject const &configuration) {
  QStringList bands;
  for (QStringList const &row : pidRows(configuration)) {
    bands << QStringLiteral("%1/%2/%3").arg(row[3], row[4], row[5]);
  }
  return bands.isEmpty() ? QObject::tr("not recorded") : bands.join(QStringLiteral(" | "));
}

QString calibrationSummary(QJsonObject const &configuration) {
  QStringList parts;
  for (QJsonValue const &value : configuration.value("plc_settings").toObject().value("calibrations").toArray()) {
    QJsonObject const calibration = value.toObject();
    QStringList       points;
    QJsonArray const  x     = calibration.value("x").toArray();
    QJsonArray const  delta = calibration.value("delta").toArray();
    for (qsizetype i{0}; i < x.size(); ++i) {
      points << QStringLiteral("%1:%2").arg(number(x[i].toDouble()), number(delta.at(i).toDouble()));
    }
    parts << QStringLiteral("%1 [%2]").arg(calibration.value("state_name").toString(calibration.value("name").toString()), points.join(QStringLiteral(", ")));
  }
  return parts.isEmpty() ? QObject::tr("not recorded") : parts.join(QStringLiteral("; "));
}

QString eventTypeName(QString const &type) {
  static QHash<QString, QString> const names{
    {core::eventtype::JOB_START, QObject::tr("Job start")},           {core::eventtype::JOB_END, QObject::tr("Job end")},
    {core::eventtype::SETPOINT_CHANGE, QObject::tr("Setpoint change")}, {core::eventtype::RAMP_START, QObject::tr("Ramp start")},
    {core::eventtype::RAMP_END, QObject::tr("Ramp end")},             {core::eventtype::PID_GAINS_CHANGE, QObject::tr("PID gains")},
    {core::eventtype::PID_BAND_CHANGE, QObject::tr("PID band")},      {core::eventtype::PID_TABLE_WRITTEN, QObject::tr("PID table written")},
    {core::eventtype::STATUS_CHANGE, QObject::tr("Status")},          {core::eventtype::PROGRAM_INTERVAL, QObject::tr("Program interval")},
    {core::eventtype::PROGRAM_CYCLE, QObject::tr("Program cycle")},   {core::eventtype::ALARM_RAISED, QObject::tr("Alarm")},
    {core::eventtype::ALARM_CLEARED, QObject::tr("Alarm cleared")},   {core::eventtype::ALARM_RECEIVED, QObject::tr("PLC alarm")},
    {core::eventtype::OPERATOR_ACTION, QObject::tr("Operator action")}, {core::eventtype::PLC_COMMAND, QObject::tr("PLC command")},
    {core::eventtype::APP_ERROR, QObject::tr("Error")},               {core::eventtype::USER_CHANGE, QObject::tr("User")}};
  return names.value(type, type);
}

QColor eventColor(QString const &type) {
  if (type.startsWith(QLatin1String("alarm")) || type == QLatin1String(core::eventtype::APP_ERROR)) {
    return QColor("#dc2626");
  }
  if (type.startsWith(QLatin1String("setpoint")) || type.startsWith(QLatin1String("ramp"))) {
    return QColor("#2563eb");
  }
  if (type.startsWith(QLatin1String("pid"))) {
    return QColor("#d97706");
  }
  if (type.startsWith(QLatin1String("job")) || type.startsWith(QLatin1String("operator")) || type == QLatin1String(core::eventtype::USER_CHANGE)) {
    return QColor("#16a34a");
  }
  return QColor("#64748b");
}

QString integrityText(int integrity) {
  switch (integrity) {
    case 0:
      return QObject::tr("✓ samples verified against their content hash");
    case 1:
      return QObject::tr("⚠ samples do not match their content hash (modified or damaged after saving)");
    default:
      return QObject::tr("no content hash (experiment from Insight or an older version)");
  }
}

}
