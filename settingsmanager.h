#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QVariant>

#include "file_operation/filehandler.h"
#include "file_operation/stringpathprovider.h"
#include "logger.h"
#include "systemvaluelist.h"
#include "uilanguage.h"

namespace control2 {

/**
 * @brief All manipulations with user settings occure through this class.
 */
class SettingsManager
    : public QObject
    , public FileHandler {
  Q_OBJECT

public:
  SettingsManager(Logger *logger, QString const &filePath, QObject *parent = nullptr);
  bool                          saveToSettingsFile();   // use FileHandler interface inside
  bool                          loadFromSettingsFile(); // use FileHandler interface inside
  QVariantMap                   settingsMap() const;
  SystemValueList              &plcSettingsSystemValueList();
  QVariantList                  commitSystemValues(); // returns SystemValues as VariantList
  void                          setPlcSettings(QVariantMap const &plcSettings);
  void                          setStatesSettings(QVariantList const &statesSettings);
  QMap<QString, float>          getSettingPairs() const;
  void                          applySettingsFromPairs(QMap<QString, float> const &plcPairs);
  QMap<quint32, QString> const &getAlarmsMap() const;
  UiLanguage                    uiLanguage() const noexcept {
    return m_language;
  }
signals:
  // Settings updated from file or plc pairs.
  // Set of general settings updates its widget automatically, but another (pid, calibration)
  // need be informed.
  void settingsPulled();

private:
  // Creates new items or update existing in m_settingValueList from general settings.
  void                   pullSystemValues();
  void                   makeAlarmsMap();
  void                   getUiLanguage();
  Logger                *m_logger;
  QString                m_filePath;
  QVariantMap            m_settingsMap;      // static settings
  SystemValueList        m_settingValueList; // dynamic general settings
  QMap<quint32, QString> m_alarmsMap;
  UiLanguage             m_language;

  // FileHandler interface
protected:
  bool writeData(QFile &file) override;
  bool readData(QFile &file) override;
};

inline bool SettingsManager::saveToSettingsFile() {
  StringPathProvider settings_path(m_filePath, m_logger);
  return saveToFile(&settings_path);
}

inline bool SettingsManager::loadFromSettingsFile() {
  StringPathProvider settings_path(m_filePath, m_logger);
  return loadFromFile(&settings_path);
}

inline QVariantMap SettingsManager::settingsMap() const {
  return m_settingsMap;
}

// Changable plc settings
inline SystemValueList &SettingsManager::plcSettingsSystemValueList() {
  return m_settingValueList;
}

inline QMap<quint32, QString> const &SettingsManager::getAlarmsMap() const {
  return m_alarmsMap;
}
} // namespace control2

#endif // SETTINGSMANAGER_H
