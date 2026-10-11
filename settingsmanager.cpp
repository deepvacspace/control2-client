#include "settingsmanager.h"

#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "settingstransformer.h"
#include "stringpathprovider.h"
#include "utils.h"

namespace deepvaccontrol {
SettingsManager::SettingsManager(Logger *logger, QString const &filePath, QObject *parent)
    : QObject{parent}, m_logger{logger}, FileHandler{}, m_filePath{filePath} {
}

bool SettingsManager::loadFrom(QString const &path) {
  StringPathProvider provider(path, m_logger);
  return loadFromFile(&provider);
}

QString SettingsManager::fileProblem(QString const &path) {
  QFile file(path);
  if (!file.exists()) {
    return tr("The file does not exist.");
  }
  if (!file.open(QIODevice::ReadOnly)) {
    return tr("The file cannot be opened (%1).").arg(file.errorString());
  }
  QByteArray const data = file.readAll();
  if (data.trimmed().isEmpty()) {
    return tr("The file is empty (it was probably cut off while being saved).");
  }
  QJsonParseError     error{};
  QJsonDocument const doc = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError) {
    QByteArray const before = data.left(error.offset);
    int const        line   = static_cast<int>(before.count('\n')) + 1;
    int const        column = static_cast<int>(error.offset - (before.lastIndexOf('\n') + 1)) + 1;
    return tr("The file is not valid JSON: %1 at line %2, column %3.").arg(error.errorString()).arg(line).arg(column);
  }
  if (!doc.isObject() || !doc.object().contains(QStringLiteral("states"))) {
    return tr("The file has no list of chamber values (\"states\").");
  }
  return {};
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
  emit plcSettingsChanged();
}

void SettingsManager::setStatesSettings(QVariantList const &statesSettings) {
  m_settingsMap["states"] = statesSettings;
  saveToSettingsFile();
}

bool SettingsManager::setOpcSettings(bool enabled, QVariantMap const &opcSettings) {
  QVariantMap appSettings       = m_settingsMap.value("app_settings").toMap();
  appSettings["opc_server"]     = enabled;
  m_settingsMap["app_settings"] = appSettings;
  m_settingsMap["opc_settings"] = opcSettings;

  bool const saved = saveToSettingsFile();
  emit opcSettingsChanged();
  return saved;
}

bool SettingsManager::setTcpServer(QString const &address, quint16 port) {
  QVariantMap tcpServer       = m_settingsMap.value("tcp_server").toMap();
  tcpServer["server_ip"]      = address;
  tcpServer["server_port"]    = port;
  m_settingsMap["tcp_server"] = tcpServer;
  return saveToSettingsFile();
}

bool SettingsManager::setSection(QString const &key, QVariantMap const &section) {
  m_settingsMap[key] = section;
  return saveToSettingsFile();
}

bool SettingsManager::setEntries(QVariantMap const &entries) {
  for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
    m_settingsMap[it.key()] = it.value();
  }
  bool const saved = saveToSettingsFile();
  emit appSettingsChanged();
  return saved;
}

bool SettingsManager::setChambers(QVariantList const &chambers) {
  m_settingsMap["chambers"] = chambers;
  return saveToSettingsFile();
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
