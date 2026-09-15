#ifndef SETTINGSTRANSFORMER_H
#define SETTINGSTRANSFORMER_H

#include <QVariantList>

namespace deepvaccontrol {

// Functions to convert complex settings structures "pid" and "calibration"
// to QMap<QString, float>

QMap<QString, float> pidVariantToStringMap(QVariantMap const &pidVariantMap);
QVariantMap          updatePidValuesFromMap(QMap<QString, float> const &map, QVariantMap pidVariantMap);

// Functions to update complex settings structures "pid" and "calibration"
// with QMap<QString, float>

QMap<QString, float> calibrationVariantToStringMap(QVariantMap const &calibrationVariantMap);
QVariantMap          updateCalibrationValuesFromMap(QMap<QString, float> const &map, QVariantMap calibrationVariantMap);
} // namespace deepvaccontrol

#endif // SETTINGSTRANSFORMER_H
