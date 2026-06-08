#include "settingswidget.h"

#include <QTabWidget>
#include <QVBoxLayout>

#include "alarmlistwidget.h"
#include "constants.h"
#include "settingslistwidget.h"
#include "utils.h"

namespace control2 {
SettingsWidget::SettingsWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  initGui();

  connect(m_resHolder->settings, &SettingsManager::settingsPulled, this, &SettingsWidget::updatePidsAndCalibrations);
}

void SettingsWidget::setPids(QVariantList const &pidVariantList) {
  if (pidVariantList.size() != m_pidWidgets.size()) {
    qWarning() << "Wrong pids data";
    return;
  }

  for (int i{0U}; i < pidVariantList.size(); ++i) {
    m_pidWidgets[i]->setDataVariant(pidVariantList[i].toMap());
  }
}

QVariantList SettingsWidget::getPidsVariantList() const {
  QVariantList ret;

  for (int i{0U}; i < m_pidWidgets.size(); ++i) {
    ret.push_back(m_pidWidgets[i]->getDataVariant());
  }
  return ret;
}

void SettingsWidget::setCalibrations(QVariantList const &calibVariantList) {
  if (calibVariantList.size() != m_calibWidgets.size()) {
    qWarning() << "Wrong calibration data";
    return;
  }

  for (int i{0U}; i < calibVariantList.size(); ++i) {
    m_calibWidgets[i]->setDataVariant(calibVariantList[i].toMap());
  }
}

QVariantList SettingsWidget::getCalibrationsVariantList() const {
  QVariantList ret;

  for (int i{0U}; i < m_calibWidgets.size(); ++i) {
    ret.push_back(m_calibWidgets[i]->getDataVariant());
  }
  return ret;
}

void SettingsWidget::updatePidsAndCalibrations() {
  setPids(m_resHolder->settings->settingsMap()["plc_settings"].toMap()["pids"].toList());
  setCalibrations(m_resHolder->settings->settingsMap()["plc_settings"].toMap()["calibrations"].toList());
}

void SettingsWidget::initGui() {
  // tabWidget / buttons
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(3 * utils::ssf());

  auto *tabWidget = new QTabWidget(this);
  tabWidget->setStyleSheet(utils::readStyleFromFile(":/styles/tabBar.qss")
                             .arg(40 * utils::ssf())
                             .arg(100 * utils::ssf())
                             .arg(100 * utils::ssf()));
  mainLay->addWidget(tabWidget);

  // 1 - general plc settings
  auto *settingsList = new SettingsListWidget(m_resHolder, this);
  tabWidget->addTab(settingsList, tr("Параметры работы"));

  QVariantMap const settingsVariantMap    = m_resHolder->settings->settingsMap();
  QVariantMap const plcSettingsVariantMap = settingsVariantMap["plc_settings"].toMap();

  // 2 - pids
  QVariantList pidListVariant = plcSettingsVariantMap["pids"].toList();
  for (int i{0U}; i < pidListVariant.size(); ++i) {
    QVariantMap pidVariantMap = pidListVariant[i].toMap();
    auto       *pidWidget     = new PidWidget(pidVariantMap, this);
    tabWidget->addTab(pidWidget, pidVariantMap["label"].toString());
    m_pidWidgets.push_back(pidWidget);
  }

  // 3 - calibrations
  QVariantList calibListVariant = plcSettingsVariantMap["calibrations"].toList();
  for (int i{0U}; i < calibListVariant.size(); ++i) {
    QVariantMap   calibVariantMap = calibListVariant[i].toMap();
    QString const stateSystemValueName{calibVariantMap["state_name"].toString()};
    QString const stateRawSystemValueName{stateSystemValueName + "_raw"};

    auto *curValue    = m_resHolder->states->getSysValue(stateSystemValueName);
    auto *curRawValue = m_resHolder->states->getSysValue(stateRawSystemValueName);

    auto *calibWidget = new CalibrationWidget(calibVariantMap, curValue, curRawValue, this);
    tabWidget->addTab(calibWidget, calibVariantMap["label"].toString());
    m_calibWidgets.push_back(calibWidget);
  }

  // 4 - аварии
  auto *alarmListWidget = new AlarmListWidget(m_resHolder, this);
  tabWidget->addTab(alarmListWidget, tr("Аварии"));

  auto *btnsLay = new QHBoxLayout();
  mainLay->addLayout(btnsLay);

  auto *sendBtn = new QPushButton(tr("Загрузить настройки в ПЛК"), this);
  auto *getBtn  = new QPushButton(tr("Получить настройки из ПЛК"), this);
  sendBtn->setFixedHeight(control2::UI_BTN_HEIGHT);
  getBtn->setFixedHeight(control2::UI_BTN_HEIGHT);

  btnsLay->addWidget(sendBtn);
  btnsLay->addWidget(getBtn);

  connect(sendBtn, &QPushButton::clicked, this, &SettingsWidget::commitSettings);
  connect(getBtn, &QPushButton::clicked, this, [this] {
    m_resHolder->tcpGate->sendTextCmd("get_settings");
  });
}

void SettingsWidget::commitSettings() {
  // "plc_settings"."general" (alarms also)
  QVariantList const newGeneral = m_resHolder->settings->commitSystemValues();

  // "plc_settings"."pids"
  QVariantList newSettingsPids;
  for (int i{0U}; i < m_pidWidgets.size(); ++i) {
    QVariantMap const pidVariantMap = m_pidWidgets[i]->getDataVariant();
    newSettingsPids.push_back(pidVariantMap);
  }

  // "plc_settings"."valibrations"
  QVariantList newSettingsCallibrations;
  for (int i{0U}; i < m_calibWidgets.size(); ++i) {
    QVariantMap const CalibrationVariantMap = m_calibWidgets[i]->getDataVariant();
    newSettingsCallibrations.push_back(CalibrationVariantMap);
  }

  QVariantMap newPlcSettings;
  newPlcSettings["general"]      = newGeneral;
  newPlcSettings["pids"]         = newSettingsPids;
  newPlcSettings["calibrations"] = newSettingsCallibrations;

  m_resHolder->settings->setPlcSettings(newPlcSettings);

  auto const pairs = m_resHolder->settings->getSettingPairs();
  if (m_resHolder->tcpGate) {
    m_resHolder->tcpGate->sendSettingMap(pairs);
  }
}
} // namespace control2
