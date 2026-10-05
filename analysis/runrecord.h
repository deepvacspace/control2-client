#ifndef RUNRECORD_H
#define RUNRECORD_H

#include <QColor>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include "runmanifest.h"

namespace deepvaccontrol::runrecord {

// Readable views of a run's manifest (core::RunManifest), shared by the analysis page, the report,
// the comparison and the assistant so they all describe a run the same way.

QString jobText(QJsonObject const &configuration);        // "Program "Soak -40" (program)"
QString capturedText(QJsonObject const &configuration);   // when / whether the snapshot was taken at execution

// program / profile sent to the PLC: one row per interval {#, setpoint, duration, mode}
QStringList           programColumns();
QVector<QStringList>  programRows(QJsonObject const &configuration);
QString               programSummary(QJsonObject const &configuration); // "3 intervals: 25 °C 10 min → 40 °C ramp 15 min → ..."

// PID table: one row per loop and band {loop, band, range, Kp, Ki, Kd}
QStringList          pidColumns();
QVector<QStringList> pidRows(QJsonObject const &configuration);
QString              pidSummary(QJsonObject const &configuration); // compact, comparable text

QString calibrationSummary(QJsonObject const &configuration); // "temp: x … → delta …"

QString eventTypeName(QString const &type); // "Setpoint change"
QColor  eventColor(QString const &type);

QString integrityText(int integrity); // RunStore::Integrity as text

} // namespace deepvaccontrol::runrecord

#endif // RUNRECORD_H
