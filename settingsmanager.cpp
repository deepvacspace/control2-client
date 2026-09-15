#include "settingsmanager.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>

#include "settingstransformer.h"
#include "utils.h"

namespace deepvaccontrol {
SettingsManager::SettingsManager(Logger *logger, QString const &filePath, QObject *parent)
    : QObject{parent}, m_logger{logger}, FileHandler{}, m_filePath{filePath} {
}

bool SettingsManager::writeData(QFile &file) {
  file.write(QJsonDocument::fromVariant(m_settingsMap).toJson());

  return true;
}

bool SettingsManager::readData(QFile &file) {
  m_settingsMap = utils::parseJsonToVariant(file.readAll(), file.fileName());
  if (m_settingsMap.empty()) {
    return false;
  }

  makeAlarmsMap();
  pullSystemValues();
  getUiLanguage();

  return true;
}

void SettingsManager::pullSystemValues() {
  if (0 == m_settingValueList.size()) {
    m_settingValueList.addWithVariantList(m_settingsMap["plc_settings"].toMap()["general"].toList());
  } else {
    auto const           generalSettingsList = m_settingsMap["plc_settings"].toMap()["general"].toList();
    QMap<QString, float> generalMap;
    for (auto const &setting : generalSettingsList) {
      auto settingMap                           = setting.toMap();
      generalMap[settingMap["name"].toString()] = settingMap["value"].toFloat();
    }

    auto const names{m_settingValueList.getNames()};
    for (auto const &name : names) {
      m_settingValueList.getSysValue(name)->setValue(generalMap[name]);
    }
  }

  emit settingsPulled();
}

void SettingsManager::makeAlarmsMap() {
  QVariantList alarmsVariantList = m_settingsMap["alarms"].toList();
  m_alarmsMap.clear();
  for (int i{0U}; i < alarmsVariantList.size(); ++i) {
    QVariantMap const alarmMap{alarmsVariantList[i].toMap()};
    quint32 const     alarmIndex{alarmMap["index"].toUInt()};
    QString const     alarmLabel{alarmMap["label"].toString()};
    m_alarmsMap[alarmIndex] = alarmLabel;
  }
}

void SettingsManager::getUiLanguage() {
  m_language = UiLanguage::Russian;
  if (m_settingsMap["app_settings"].isValid() && m_settingsMap["app_settings"].toMap()["language"].isValid()) {
    auto const lang = m_settingsMap["app_settings"].toMap()["language"].toString();
    if (lang == "en") {
      m_language = UiLanguage::English;
    }
  }
}

QVariantList SettingsManager::commitSystemValues() {
  return m_settingValueList.getDataVaraint();
}

void SettingsManager::setPlcSettings(QVariantMap const &plcSettings) {
  m_settingsMap["plc_settings"] = plcSettings;
  saveToSettingsFile();
}

void SettingsManager::setStatesSettings(QVariantList const &statesSettings) {
  m_settingsMap["states"] = statesSettings;
  saveToSettingsFile();
}

QMap<QString, float> SettingsManager::getSettingPairs() const {
  QMap<QString, float> ret;

  // general
  QVariantList const general = m_settingsMap["plc_settings"].toMap()["general"].toList();
  for (auto const &settingVariant : general) {
    QVariantMap const settingMap       = settingVariant.toMap();
    ret[settingMap["name"].toString()] = settingMap["value"].toFloat();
  }

  // pids
  QVariantList const pids = m_settingsMap["plc_settings"].toMap()["pids"].toList();
  for (int i{0U}; i < pids.size(); ++i) {
    ret.insert(pidVariantToStringMap(pids[i].toMap()));
  }

  // calibartions
  QVariantList const calibrations = m_settingsMap["plc_settings"].toMap()["calibrations"].toList();
  for (int i{0U}; i < calibrations.size(); ++i) {
    ret.insert(calibrationVariantToStringMap(calibrations[i].toMap()));
  }

  return ret;
}

void SettingsManager::applySettingsFromPairs(QMap<QString, float> const &plcPairs) {
  // general
  QVariantList       newGeneral;
  QVariantList const general = m_settingsMap["plc_settings"].toMap()["general"].toList();
  for (auto const &settingVariant : general) {
    QVariantMap   settingMap = settingVariant.toMap();
    QString const settingName{settingMap["name"].toString()};
    if (plcPairs.contains(settingName)) {
      float const newValue{plcPairs[settingName]};
      settingMap["value"] = newValue;
      newGeneral.push_back(settingMap); // push updated setting map
    } else {
      m_logger->stream(Logger::Level::ERR) << "No setting" << settingName << "in local settings";
      return;
    }
  }

  // pids
  QVariantList       newPidsList;
  QVariantList const pids = m_settingsMap["plc_settings"].toMap()["pids"].toList();
  for (int i{0U}; i < pids.size(); ++i) {
    QVariantMap newPid = updatePidValuesFromMap(plcPairs, pids[i].toMap());
    newPidsList.push_back(newPid);
  }

  // calibrations
  QVariantList       newCalibrationsList;
  QVariantList const calibrations = m_settingsMap["plc_settings"].toMap()["calibrations"].toList();
  for (int i{0U}; i < calibrations.size(); ++i) {
    QVariantMap newCalibration = updateCalibrationValuesFromMap(plcPairs, calibrations[i].toMap());
    newCalibrationsList.push_back(newCalibration);
  }

  QVariantMap newPlcSettings;
  newPlcSettings["general"]      = newGeneral;
  newPlcSettings["pids"]         = newPidsList;
  newPlcSettings["calibrations"] = newCalibrationsList;

  setPlcSettings(newPlcSettings);

  pullSystemValues(); // reinit system values
}

} // namespace deepvaccontrol
