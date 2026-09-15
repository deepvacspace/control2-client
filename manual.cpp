#include "manual.h"

#include <QVBoxLayout>

#include "constants.h"
#include "delayinfodialog.h"
#include "delayselectdialog.h"
#include "labelplus.h"
#include "lineeditnum.h"
#include "lineedittime.h"
#include "statuslabel.h"
#include "utils.h"

namespace deepvaccontrol {

constexpr static quint8 const TEMP_RAMP_CHECHED{16U}; // bit in flags SystemValue
constexpr static quint8 const TEMP_HOLD_CHECHED{15U}; // bit in flags SystemValue

Manual::Manual(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}
    , m_resHolder{resHolder}
    , m_accessLevel{resHolder->users->getCurrentAccessLevel()}
    , m_modelFlags{nullptr} {
  auto settingsMap = m_resHolder->settings->settingsMap();
  if (settingsMap.contains("control_params") && (!settingsMap["control_params"].toList().empty())) {
    m_modelSysValuesList = new SystemValueList(m_resHolder->logger, this);

    QVariantList const controlParamsList = settingsMap["control_params"].toList();
    m_modelSysValuesList->addWithVariantList(controlParamsList);

    // 'flags' param in both m_modelSysValuesList and m_model is a uint value
    // it should be interpret as
    // 0b00..00 0011
    //            ^^-- 'duration flag' ignored
    //            '--- temp enable flag

    // Create empty manual model with 0 intervals
    m_model = new ProgramModel(controlParamsList, ProgramModel::ProgType::SETPOINT, this);

    // Update control btns activity due to selected states
    m_modelFlags = m_modelSysValuesList->getSysValue("flags");
    if (nullptr != m_modelFlags) {
      connect(m_modelFlags, &SystemValue::valueChanged, this, &Manual::updateGui);
    }
  } else {
    m_resHolder->logger->stream(Logger::Level::ERR) << "There are no control_params or they are empty in settings.json";
  }

  initGui();
  updateGui();

  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &Manual::updateGui);
  connect(m_resHolder->users, &AccessManager::userSelected, this, &Manual::handleAccessChange);
}

void Manual::handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel) {
  if (newLevel == m_accessLevel) {
    return;
  }

  m_accessLevel = newLevel;
  updateGui();
}

void Manual::initGui() {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_startBtn = new QPushButton(tr("Пуск"), this);
  m_startBtn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());
  m_startBtn->setFont(QFont("MS Shell Dlg 2", 12 * utils::ssf(), 75));
  m_startDelayedBtn = new QPushButton(tr("Отлож.\nпуск"), this);
  m_startDelayedBtn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());
  m_startDelayedBtn->setFont(QFont("MS Shell Dlg 2", 12 * utils::ssf(), 75));

  m_stopBtn = new StopButton(this);

  auto *controlLay = new QHBoxLayout();
  controlLay->addWidget(m_startBtn);
  controlLay->addWidget(m_startDelayedBtn);
  controlLay->addWidget(m_stopBtn);
  controlLay->addWidget(new StatusLabel(m_resHolder, this));
  controlLay->addStretch();

  QMap<QString, QString>         descr;
  QList<QPair<QString, QString>> paramOrder;
  paramOrder.push_back({"temp", tr("Температура, °C")});
  paramOrder.push_back({"hum", tr("Относительная влажность, %")});
  paramOrder.push_back({"pres", tr("Давление, мм рт. ст.")});
  paramOrder.push_back({"solar", tr("Лампа солнечной радиации")});

  QList<QVBoxLayout *> statesLaysList;
  QVector<int>         layFillOrder{0, 1, 1, 0};
  int                  paramCount{0U}; // number of added params
  bool                 tempAlwaysEnable{true};

  if (m_modelSysValuesList->contains("pres")) {
    tempAlwaysEnable = false;
  }

  for (int i{0U}; i < paramOrder.size(); ++i) {
    auto const &ckeckedParam = paramOrder[i];
    auto const &name         = ckeckedParam.first;
    auto const &label        = ckeckedParam.second;

    if (!m_modelSysValuesList->contains(name)) {
      continue;
    }

    int const indexInSysValModel{static_cast<int>(m_modelSysValuesList->getNames().indexOf(name))};

    QGroupBox *newItem = nullptr;

    if (name == "temp") {
      auto *tempWidget = new TempSetWidget(indexInSysValModel,
                                           m_modelSysValuesList,
                                           m_resHolder->states->getSysValue(name),
                                           false /*speedUnitIsHour*/,
                                           tempAlwaysEnable /*alwaysEnable*/,
                                           this);
      tempWidget->setTitle(label);
      newItem = tempWidget;
    }
    // else if (name == "solar_en") {
    //   auto solarWidget = new SolarSetWidget(i, m_manualModel, states->getVar(name), this);
    //   newItem          = solarWidget;
    // } else if (name == "lum") {
    //   auto lumWidget = new LumSetWidget(i, m_manualModel, states->getVar(name), this);
    //   newItem        = lumWidget;
    else {
      auto stateSetWidget
        = new CommonSetWidget(indexInSysValModel, m_modelSysValuesList, m_resHolder->states->getSysValue(name), this);
      stateSetWidget->setTitle(label);
      newItem = stateSetWidget;
    }

    newItem->setFixedWidth(300 * utils::ssf());
    m_stateWidgets.insert(paramCount, newItem);

    auto *stateVisCover = new QVBoxLayout();
    stateVisCover->addWidget(newItem);
    stateVisCover->addStretch();
    if ((statesLaysList.size() - 1) < layFillOrder.at(paramCount)) {
      statesLaysList << new QVBoxLayout();
    }
    statesLaysList.at(layFillOrder.at(paramCount))->addLayout(stateVisCover);
    ++paramCount;
  }

  auto statesLay = new QHBoxLayout();
  for (auto const &item : statesLaysList) {
    item->addStretch(10);
    statesLay->addLayout(item);
  }
  statesLay->addStretch(10);
  mainLay->addLayout(controlLay);
  mainLay->addLayout(statesLay);
  mainLay->addStretch();

  setStyleSheet(utils::readStyleFromFile(":/styles/stateVisBox.qss")
                  .arg(4 * utils::ssf())
                  .arg(8.5 * utils::ssf())
                  .arg(0 * utils::ssf())
                  .arg(25 * utils::ssf())
                  .arg(26 * utils::ssf())
                + utils::readStyleFromFile(":/styles/checkBoxBig.qss")
                + QString("QCheckBox::indicator{width: %1px; height: %1px;}").arg(26 * utils::ssf())
                + utils::readStyleFromFile(":/styles/lampSlider.qss"));

  connect(m_startBtn, &QPushButton::clicked, this, &Manual::sendProgram);

  connect(m_stopBtn, &StopButton::confirmPressed, this, [this] {
    m_resHolder->tcpGate->sendTextCmd("stop");
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Работа камеры остановлена пользователем"));
  });

  connect(m_startDelayedBtn, &QPushButton::clicked, this, [this] {
    auto *delaySelectDialog = new DelaySelectDialog(this);
    connect(delaySelectDialog, &DelaySelectDialog::startTimeSelected, this, [this](QDateTime startTime) {
      auto *delayInfoDialog = new DelayInfoDialog(tr("Работа по уставке"), startTime, this);
      connect(delayInfoDialog, &QDialog::accepted, this, &Manual::sendProgram);
      delayInfoDialog->open();
    });
    delaySelectDialog->open();
  });
}

void Manual::updateGui() {
  HardwareStatus::HardwareStatusEnum const hardWorkStatus{m_resHolder->hard_status->getHardwareStatus()};

  quint32    u32_flags = static_cast<quint32>(m_modelFlags->getValue());
  bool const someStateEnabled{0U != (u32_flags & /*state enbable mask*/ 0b111110)};

  m_stopBtn->setEnabled((HardwareStatus::HardwareStatusEnum::WORK == hardWorkStatus)
                        && (HardwareStatus::HardwareStatusEnum::NONE != hardWorkStatus)
                        && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel));

  bool const enableStart{((HardwareStatus::HardwareStatusEnum::IDLE == hardWorkStatus)
                          || (HardwareStatus::HardwareStatusEnum::WORK == hardWorkStatus)
                          || (HardwareStatus::HardwareStatusEnum::FINISH == hardWorkStatus))
                         && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel) && someStateEnabled};

  m_startBtn->setEnabled(enableStart);

  bool const enableStartDelay{((HardwareStatus::HardwareStatusEnum::IDLE == hardWorkStatus)
                               || (HardwareStatus::HardwareStatusEnum::FINISH == hardWorkStatus))
                              && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel) && someStateEnabled};

  m_startDelayedBtn->setEnabled(enableStartDelay);
}

void Manual::sendProgram() {
  constexpr static quint32 const LONG_TIME{100 * 3600 - 1}; // 316224000U = 3660 days

  int const tempParamIndex{static_cast<int>(m_modelSysValuesList->getNames().indexOf("temp"))};
  bool      speedUnitIsHour{false};

  quint32 const flags_cur{static_cast<quint32>(m_modelSysValuesList->getSysValue("flags")->getValue())};
  bool const    tempChecked{m_modelSysValuesList->getSysValue("flags")->getBitOfUInt(tempParamIndex)};
  bool const    rampChecked{tempChecked && m_modelSysValuesList->getSysValue("flags")->getBitOfUInt(TEMP_RAMP_CHECHED)};
  bool const    holdChecked{tempChecked && m_modelSysValuesList->getSysValue("flags")->getBitOfUInt(TEMP_HOLD_CHECHED)};

  float const temp_cur{m_resHolder->states->getSysValue("temp")->getValue()};
  float const temp_set{m_modelSysValuesList->getSysValue("temp")->getValue()};
  float const ramp_set{m_modelSysValuesList->getSysValue("temp_speed")->getValue()};
  float const hold_set{m_modelSysValuesList->getSysValue("hold_time")->getValue()};

  while (m_model->getStandardModel()->columnCount() > 0) {
    m_model->delItem(0);
  }

  // delete first interval if no ramp
  for (int col{0U}; col < 2; ++col) {
    QList<QStandardItem *> columnToAdd;
    for (int i{0U}; i < m_model->getParamListOrder().size(); ++i) {
      auto *standardItem = new QStandardItem();
      columnToAdd.append(standardItem);
    }
    m_model->getStandardModel()->insertColumn(col, columnToAdd);
  }

  quint32 flags0_set{0U}; // flags for the first interval;
  quint32 flags1_set{0U}; // flags for the second interval;

  // model 'flags' ~= system values model's 'flags'
  // but directly copy only first byte with channels
  flags0_set = flags_cur & 0x07;
  flags1_set = flags_cur & 0x07;

  // set param values in model
  auto names = m_modelSysValuesList->getNames();
  for (auto const &paramName : names) {
    m_model->setParamValue(0, paramName, m_modelSysValuesList->getSysValue(paramName)->getValue());
    m_model->setParamValue(1, paramName, m_modelSysValuesList->getSysValue(paramName)->getValue());
  }

  m_model->setParamValue(1, "duration", holdChecked ? hold_set : LONG_TIME);
  utils::setBitOfDword(flags1_set, deepvaccontrol::AWAIT_BIT, holdChecked);
  m_model->setParamValue(1, "flags", flags1_set);

  if (rampChecked) {
    m_model->setParamValue(0, "duration", fabs((temp_cur - temp_set) / ramp_set * (speedUnitIsHour ? 3600 : 60)));
    utils::setBitOfDword(flags0_set, deepvaccontrol::RAMP_BIT, true);
    m_model->setParamValue(0, "flags", flags0_set);
  } else {
    m_model->delItem(0);
  }

  if (m_resHolder->aiAdvisorEnabled) {
    utils::showAiAdvisorStartPromptAndRun(this);
  }

  m_model->printToDebug();
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, m_model->makeSendData());
  m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Запуск работы по уставке"));
}

TempSetWidget::TempSetWidget(quint32          index,
                             SystemValueList *manualModel,
                             SystemValue     *currentVal,
                             bool             speedUnitIsHour,
                             bool             alwaysEnable,
                             QWidget         *parent)
    : QGroupBox{parent}, m_modelSysValuesList{manualModel}, m_currentVal{currentVal} {
  auto *flagsSysValue = manualModel->getSysValue("flags");
  setCheckable(!alwaysEnable);
  if (alwaysEnable) {
    setChecked(true);
    flagsSysValue->setBitOfUInt(index, true); // NOTE important
  } else {
    setChecked(flagsSysValue->getBitOfUInt(index));
  }

  if (!alwaysEnable) {
    connect(this, &QGroupBox::toggled, this, [index, flagsSysValue](bool value) {
      flagsSysValue->setBitOfUInt(index, value);
    });
  }

  auto mainLay = new QVBoxLayout(this);

  auto *setValLay = new QGridLayout();
  setValLay->addWidget(new QLabel(tr("Текущее значение:"), this), 0, 0);
  setValLay->addWidget(new LabelPlus(m_currentVal, this), 0, 1);

  setValLay->addWidget(new QLabel(tr("Новая уставка:"), this), 1, 0);
  setValLay->addWidget(new LineEditNum(manualModel->getSysValue("temp")), 1, 1);

  QString speedUnitString = speedUnitIsHour ? tr("час") : tr("мин");

  auto *rampBox = new QGroupBox(tr("Скорость выхода, °C/") + speedUnitString + ":", this);
  rampBox->setCheckable(true);
  rampBox->setChecked(false);
  auto *rampBoxLay = new QGridLayout(rampBox);
  rampBoxLay->setContentsMargins(3, 3, 3, 3); // left, top, right, and bottom

  auto *tempSpeedSysValue = new SystemValue("temp_speed", 1, this);
  tempSpeedSysValue->setPrec(2);
  tempSpeedSysValue->setMin(0.01 * (speedUnitIsHour ? 60 : 1));
  tempSpeedSysValue->setMax(10 * (speedUnitIsHour ? 60 : 1));
  manualModel->add(tempSpeedSysValue);
  rampBoxLay->addWidget(new LineEditNum(tempSpeedSysValue), 0, 1);
  rampBoxLay->setColumnStretch(0, 10);

  auto *holdTimeBox = new QGroupBox(tr("Время выдержки"), this);
  holdTimeBox->setCheckable(true);
  holdTimeBox->setChecked(false);
  auto *holdTimeBoxLay = new QGridLayout(holdTimeBox);
  holdTimeBoxLay->setContentsMargins(3, 3, 3, 3);

  auto *holdTimeSysValue = new SystemValue("hold_time", 3600, this);
  holdTimeSysValue->setMin(60);
  holdTimeSysValue->setMax(3599999);
  manualModel->add(holdTimeSysValue);
  holdTimeBoxLay->addWidget(new LineEditTime(holdTimeSysValue), 0, 1);
  holdTimeBoxLay->setColumnStretch(0, 10);

  mainLay->addLayout(setValLay);
  mainLay->addWidget(rampBox);
  mainLay->addWidget(holdTimeBox);

  connect(holdTimeBox, &QGroupBox::toggled, this, [flagsSysValue](bool value) {
    flagsSysValue->setBitOfUInt(TEMP_HOLD_CHECHED, value);
  });
  connect(rampBox, &QGroupBox::toggled, this, [flagsSysValue](bool value) {
    flagsSysValue->setBitOfUInt(TEMP_RAMP_CHECHED, value);
  });
}

CommonSetWidget::CommonSetWidget(quint32 index, SystemValueList *manualModel, SystemValue *currentVal, QWidget *parent)
    : QGroupBox{parent}, m_modelSysValuesList{manualModel}, m_currentVal{currentVal} {
  auto *flagsSysValue = manualModel->getSysValue("flags");
  setCheckable(true);
  setChecked(flagsSysValue->getBitOfUInt(index));

  connect(this, &QGroupBox::toggled, this, [index, flagsSysValue](bool value) {
    flagsSysValue->setBitOfUInt(index, value);
  });

  auto *mainLay = new QVBoxLayout(this);

  auto *setValLay = new QGridLayout();
  setValLay->addWidget(new QLabel(tr("Текущее значение:"), this), 0, 0);
  setValLay->addWidget(new LabelPlus(m_currentVal, this), 0, 1);

  QString const stateName{manualModel->getNames().at(index)};
  setValLay->addWidget(new QLabel(tr("Новая уставка:"), this), 1, 0);
  setValLay->addWidget(new LineEditNum(manualModel->getSysValue(stateName)), 1, 1);
  mainLay->addLayout(setValLay);

  setStyleSheet(utils::readStyleFromFile(":/styles/checkBox")
                + QString("QCheckBox::indicator{width: %1px; height: %1px;}").arg(26 * utils::ssf()));
}

} // namespace deepvaccontrol
