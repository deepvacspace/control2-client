#include "settingswidget.h"

#include "applicationsettingswidget.h"

#include <algorithm>

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGridLayout>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

#include "alarmlistwidget.h"
#include "connectionswidget.h"
#include "datawidget.h"
#include "constants.h"
#include "settingslistwidget.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
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
  theme::setFlatTabs(tabWidget);
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
    if (m_resHolder->states->contains("temp")) {
      pidWidget->setCurrentValue(m_resHolder->states->getSysValue("temp"));
    }
    if (m_resHolder->states->contains("temp_ref")) {
      pidWidget->setSetpointValue(m_resHolder->states->getSysValue("temp_ref")); // selects the band on the PLC
    }
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

  // App-only AI setting (not part of the settings sent to the PLC)
  double const ssf{utils::ssf()};
  auto        *aiSettingsWidget = new QWidget(this);
  {
    auto *pageLay = new QVBoxLayout(aiSettingsWidget);
    pageLay->setContentsMargins(0, 8 * ssf, 0, 0);
    auto *card = new QFrame(aiSettingsWidget);
    card->setObjectName("pageCard");
    pageLay->addWidget(card);
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
    cardLay->setSpacing(8 * ssf);
    auto *title = new QLabel(tr("AI"), card);
    title->setObjectName("cardTitle");
    cardLay->addWidget(title);

    auto *row = new QFrame(card);
    row->setObjectName("outputRow");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(12 * ssf, 10 * ssf, 12 * ssf, 10 * ssf);
    rowLay->setSpacing(12 * ssf);
    auto *icon = new QLabel(row);
    int const iconSize{static_cast<int>(22 * ssf)};
    theme::setThemedIcon(icon, "ai", iconSize, "muted");
    rowLay->addWidget(icon);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(2 * ssf);
    auto *rowTitle = new QLabel(tr("AI Advisor"), row);
    rowTitle->setObjectName("rowTitle");
    auto *rowText = new QLabel(tr("Before a job starts, offer to search suitable PID parameters with the AI Advisor."), row);
    rowText->setProperty("muted", true);
    texts->addWidget(rowTitle);
    texts->addWidget(rowText);
    rowLay->addLayout(texts, 1);

    // Off / On
    auto *group    = new QButtonGroup(row);
    auto *segments = new QHBoxLayout();
    segments->setSpacing(0);
    QStringList const texts2{tr("Off"), tr("On")};
    for (int i{0}; i < texts2.size(); ++i) {
      auto *button = new QPushButton(texts2[i], row);
      button->setCheckable(true);
      button->setProperty("segment", true);
      button->setProperty("segmentFirst", i == 0);
      button->setProperty("segmentLast", i == texts2.size() - 1);
      button->setFixedSize(80 * ssf, 30 * ssf);
      group->addButton(button, i);
      segments->addWidget(button);
    }
    group->setExclusive(true);
    group->button(m_resHolder->aiAdvisorEnabled ? 1 : 0)->setChecked(true);
    rowLay->addLayout(segments);
    cardLay->addWidget(row);
    cardLay->addStretch();

    connect(group, &QButtonGroup::idClicked, this, [this](int id) {
      m_resHolder->aiAdvisorEnabled = (id == 1);
    });
  }
  tabWidget->addTab(aiSettingsWidget, tr("AI"));

  // App-only network links: chamber gate, Deepvac Insight, OPC UA
  tabWidget->addTab(new ConnectionsWidget(m_resHolder, ConnectionsWidget::Page::TCP, this), tr("TCP"));
  tabWidget->addTab(new ConnectionsWidget(m_resHolder, ConnectionsWidget::Page::OPC, this), tr("OPC"));
  // app-only values of settings.json: refresh / polling periods, setpoint limits, program at start
  tabWidget->addTab(new ApplicationSettingsWidget(m_resHolder, this), tr("Application"));
  // databases and their backups
  tabWidget->addTab(new DataWidget(m_resHolder, this), tr("Data"));

  auto *btnsLay = new QHBoxLayout();
  mainLay->addLayout(btnsLay);

  auto *sendBtn = new QPushButton(tr("Загрузить настройки в ПЛК"), this);
  auto *getBtn  = new QPushButton(tr("Получить настройки из ПЛК"), this);
  sendBtn->setObjectName("primaryButton");
  for (QPushButton *button : {getBtn, sendBtn}) {
    button->setFixedSize(190 * utils::ssf(), 32 * utils::ssf());
  }

  btnsLay->addWidget(getBtn);
  btnsLay->addWidget(sendBtn);
  btnsLay->addStretch();

  // the PLC exchange belongs to the PLC parameter tabs only
  auto updateButtons = [this, tabWidget, settingsList, getBtn, sendBtn] {
    QWidget *page = tabWidget->currentWidget();
    bool const plcPage{(page == settingsList) || std::any_of(m_pidWidgets.cbegin(), m_pidWidgets.cend(), [page](auto *w) { return w == page; }) ||
                       std::any_of(m_calibWidgets.cbegin(), m_calibWidgets.cend(), [page](auto *w) { return w == page; })};
    getBtn->setVisible(plcPage);
    sendBtn->setVisible(plcPage);
  };
  connect(tabWidget, &QTabWidget::currentChanged, this, updateButtons);
  updateButtons();

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
} // namespace deepvaccontrol
