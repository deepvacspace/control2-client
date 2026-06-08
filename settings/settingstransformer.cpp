#include "settingstransformer.h"

namespace control2 {

QMap<QString, float> pidVariantToStringMap(QVariantMap const &pidVariantMap) {
  QString const pidName{pidVariantMap["name"].toString()};
  if (pidName.isEmpty()) {
    return {};
  }

  bool noError{false};

  quint32 const pointCount{pidVariantMap["points"].toUInt(&noError)};
  if (!noError) {
    return {};
  }

  QVariantList const pList = pidVariantMap["p"].toList();
  QVariantList const iList = pidVariantMap["i"].toList();
  QVariantList const dList = pidVariantMap["d"].toList();

  QVector<float> vecP;
  QVector<float> vecI;
  QVector<float> vecD;

  bool noErrorLocal{false};
  for (quint32 i{0U}; i < pointCount; ++i) {
    vecP.push_back(pList[i].toFloat(&noErrorLocal));
    noError |= noErrorLocal;
    vecI.push_back(iList[i].toFloat(&noErrorLocal));
    noError |= noErrorLocal;
    vecD.push_back(dList[i].toFloat(&noErrorLocal));
    noError |= noErrorLocal;
    if (!noErrorLocal) {
      return {};
    }
  }

  QMap<QString, float> ret;
  for (quint32 i{0U}; i < pointCount; ++i) {
    QString const sysNameP{QString("%1%2%3").arg(pidName).arg("p").arg(i)};
    QString const sysNameI{QString("%1%2%3").arg(pidName).arg("i").arg(i)};
    QString const sysNameD{QString("%1%2%3").arg(pidName).arg("d").arg(i)};

    ret[sysNameP] = vecP[i];
    ret[sysNameI] = vecI[i];
    ret[sysNameD] = vecD[i];
  }

  return ret;
}

QVariantMap updatePidValuesFromMap(QMap<QString, float> const &map, QVariantMap pidVariantMap) {
  QString const originalPidName{pidVariantMap["name"].toString()};
  quint32 const origianlPidPoints{pidVariantMap["points"].toUInt()};

  // filter p i d
  QMap<QString, float> pidValues;
  for (auto const &name : map.keys()) {
    if (name.startsWith(originalPidName)) {
      pidValues[name] = map[name];
    }
  }

  // count p
  quint32 points{0U};
  for (auto const &name : pidValues.keys()) {
    if (name.startsWith(QString("%1%2").arg(originalPidName).arg("p"))) {
      ++points;
    }
  }

  // count i
  quint32 points_i{0U};
  for (auto const &name : pidValues.keys()) {
    if (name.startsWith(QString("%1%2").arg(originalPidName).arg("i"))) {
      ++points_i;
    }
  }

  // check p i d count
  if (((3 * points) != pidValues.count()) || (points_i != points)) {
    return {};
  }

  QVariantList listP;
  QVariantList listI;
  QVariantList listD;

  for (quint32 i{0U}; i < points; ++i) {
    QString const nameP{QString("%1%2%3").arg(originalPidName).arg("p").arg(i)};
    QString const nameI{QString("%1%2%3").arg(originalPidName).arg("i").arg(i)};
    QString const nameD{QString("%1%2%3").arg(originalPidName).arg("d").arg(i)};
    listP.push_back(pidValues[nameP]);
    listI.push_back(pidValues[nameI]);
    listD.push_back(pidValues[nameD]);
  }

  pidVariantMap["points"] = origianlPidPoints;
  pidVariantMap["p"]      = listP;
  pidVariantMap["i"]      = listI;
  pidVariantMap["d"]      = listD;

  return pidVariantMap;
}

QMap<QString, float> calibrationVariantToStringMap(QVariantMap const &calibrationVariantMap) {
  QString const pidName{calibrationVariantMap["name"].toString()};
  if (pidName.isEmpty()) {
    return {};
  }

  bool noError{false};

  quint32 const pointCount{calibrationVariantMap["points"].toUInt(&noError)};
  if (!noError) {
    return {};
  }

  QVariantList const xList     = calibrationVariantMap["x"].toList();
  QVariantList const deltaList = calibrationVariantMap["delta"].toList();

  QVector<float> vecX;
  QVector<float> vecDelta;

  bool noErrorLocal{false};
  for (quint32 i{0U}; i < xList.size(); ++i) {
    vecX.push_back(xList[i].toFloat(&noErrorLocal));
    noError |= noErrorLocal;
    vecDelta.push_back(deltaList[i].toFloat(&noErrorLocal));
    noError |= noErrorLocal;
    if (!noErrorLocal) {
      return {};
    }
  }
  vecX.resize(pointCount, -10000);
  vecDelta.resize(pointCount, -10000);

  QMap<QString, float> ret;
  for (quint32 i{0U}; i < pointCount; ++i) {
    QString const sysNameX{QString("%1%2%3").arg(pidName).arg("x").arg(i)};
    QString const sysNameDelta{QString("%1%2%3").arg(pidName).arg("d").arg(i)};

    ret[sysNameX]     = vecX[i];
    ret[sysNameDelta] = vecDelta[i];
  }

  return ret;
}

QVariantMap updateCalibrationValuesFromMap(QMap<QString, float> const &map, QVariantMap calibrationVariantMap) {
  QString const origianlCalibrationName{calibrationVariantMap["name"].toString()};
  quint32 const origianlCalibrationPoints{calibrationVariantMap["points"].toUInt()};

  // filter x delta
  QMap<QString, float> calibrationValues;
  for (auto const &name : map.keys()) {
    if (name.startsWith(origianlCalibrationName)) {
      calibrationValues[name] = map[name];
    }
  }

  // ignore points with -10000 value (empty due to PLC logic)
  for (auto it = calibrationValues.begin(); it != calibrationValues.end();) {
    if (-10000 == it.value()) {
      it = calibrationValues.erase(it);
    } else {
      ++it;
    }
  }

  // count x
  quint32 points{0U};
  for (auto const &name : calibrationValues.keys()) {
    if (name.startsWith(QString("%1%2").arg(origianlCalibrationName).arg("x"))) {
      ++points;
    }
  }

  // check x delta count
  if ((2 * points) != calibrationValues.count()) {
    return {};
  }

  QVariantList listX;
  QVariantList listDelta;

  for (quint32 i{0U}; i < points; ++i) {
    QString const nameX{QString("%1%2%3").arg(origianlCalibrationName).arg("x").arg(i)};
    QString const nameDelta{QString("%1%2%3").arg(origianlCalibrationName).arg("d").arg(i)};
    listX.push_back(calibrationValues[nameX]);
    listDelta.push_back(calibrationValues[nameDelta]);
  }

  calibrationVariantMap["points"] = origianlCalibrationPoints;
  calibrationVariantMap["x"]      = listX;
  calibrationVariantMap["delta"]  = listDelta;

  return calibrationVariantMap;
}

} // namespace control2
