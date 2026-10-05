#include "manual.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "constants.h"
#include "experimentrecorder.h"
#include "rundetailsdialog.h"
#include "delayinfodialog.h"
#include "delayselectdialog.h"
#include "labelplus.h"
#include "lineeditnum.h"
#include "lineedittime.h"
#include "offlineadvisorstart.h"
#include "theme.h"
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
  double const ssf{utils::ssf()};
  auto        *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(10 * ssf);

  auto makeCard = [this, ssf](QString const &title, QString const &note) {
    auto *card = new QFrame(this);
    card->setObjectName("pageCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(16 * ssf, 14 * ssf, 16 * ssf, 16 * ssf);
    lay->setSpacing(8 * ssf);
    auto *top   = new QHBoxLayout();
    auto *label = new QLabel(title, card);
    label->setObjectName("cardTitle");
    top->addWidget(label);
    top->addStretch();
    if (!note.isEmpty()) {
      auto *noteLabel = new QLabel(note, card);
      noteLabel->setProperty("muted", true);
      noteLabel->setProperty("small", true);
      top->addWidget(noteLabel);
    }
    lay->addLayout(top);
    return card;
  };

  // ---- left: setpoint and job buttons
  auto *controlCard = makeCard(tr("Manual control"), {});
  auto *controlLay  = static_cast<QVBoxLayout *>(controlCard->layout());
  mainLay->addWidget(controlCard, 1);

  m_startBtn = new QPushButton(tr("Пуск"), controlCard);
  m_startBtn->setObjectName("startButton");
  m_startDelayedBtn = new QPushButton(tr("Отлож.\nпуск").replace('\n', ' '), controlCard);
  m_stopBtn         = new StopButton(controlCard);
  m_stopBtn->setProperty("outline", true);
  for (QPushButton *button : {static_cast<QPushButton *>(m_startBtn), m_startDelayedBtn, static_cast<QPushButton *>(m_stopBtn)}) {
    button->setFixedSize(130 * ssf, 40 * ssf);
    QFont font = button->font();
    font.setBold(true);
    font.setPointSizeF(QWidget::font().pointSizeF() * 1.1);
    button->setFont(font);
  }

  auto *settingsLay = new QVBoxLayout();
  settingsLay->setSpacing(6 * ssf);
  controlLay->addLayout(settingsLay);

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
      if (!tempAlwaysEnable) {
        tempWidget->setTitle(label); // checkable box: its title is the tick label
      }
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

    m_stateWidgets.insert(paramCount, newItem);
    settingsLay->addWidget(newItem);
    ++paramCount;
  }

  controlLay->addSpacing(10 * ssf);
  auto *divider = new QFrame(controlCard);
  divider->setObjectName("cardDivider");
  divider->setFixedHeight(qMax(1, static_cast<int>(1 * ssf)));
  controlLay->addWidget(divider);
  controlLay->addSpacing(10 * ssf);
  auto *buttonsLay = new QHBoxLayout();
  buttonsLay->setSpacing(10 * ssf);
  buttonsLay->addWidget(m_startBtn);
  buttonsLay->addWidget(m_startDelayedBtn);
  buttonsLay->addStretch();
  buttonsLay->addWidget(m_stopBtn);
  controlLay->addLayout(buttonsLay);
  controlLay->addSpacing(10 * ssf);

  // what Start will do first when the AI Advisor offer is enabled
  m_advisorNote = new QFrame(controlCard);
  m_advisorNote->setObjectName("advisorNote");
  auto *noteLay = new QVBoxLayout(m_advisorNote);
  noteLay->setContentsMargins(12 * ssf, 8 * ssf, 12 * ssf, 8 * ssf);
  noteLay->setSpacing(2 * ssf);
  auto *noteTitle = new QLabel(tr("AI Advisor enabled"), m_advisorNote);
  noteTitle->setObjectName("advisorNoteTitle");
  auto *noteText = new QLabel(tr("Start first offers to search suitable PID parameters (Settings → AI)."), m_advisorNote);
  noteText->setProperty("muted", true);
  noteText->setWordWrap(true);
  noteLay->addWidget(noteTitle);
  noteLay->addWidget(noteText);
  controlLay->addWidget(m_advisorNote);
  controlLay->addStretch();

  // ---- right: details recorded with the next runs
  auto *detailsCard = makeCard(tr("Run details"), tr("Stored with every run from now on"));
  auto *detailsLay  = static_cast<QVBoxLayout *>(detailsCard->layout());
  detailsLay->addSpacing(4 * ssf);
  detailsLay->addWidget(new RunDetailsPanel(m_resHolder, detailsCard));
  auto *detailsHint = new QLabel(tr("Details stay set for the following runs. Edit them after a run in Data analysis → Export → "
                                    "Edit run details…"),
                                 detailsCard);
  detailsHint->setProperty("muted", true);
  detailsHint->setProperty("small", true);
  detailsHint->setWordWrap(true);
  detailsLay->addWidget(detailsHint);
  detailsLay->addStretch();
  mainLay->addWidget(detailsCard, 1);

  theme::Colors const &c = theme::colors();
  QFont const          base = QWidget::font();
  setStyleSheet(QStringLiteral(R"(
QFrame#pageCard { background: %1; border: %2px solid %3; border-radius: %4px; }
QFrame#cardDivider { background: %3; border: none; }
QLabel { color: %5; }
QLabel[muted="true"] { color: %6; }
QLabel#cardTitle { font-weight: bold; }
QPushButton#startButton { background: %7; color: #052e16; border: none; }
QPushButton#startButton:disabled { background: %9; color: %6; }
QPushButton#stopButton[outline="true"] { background: transparent; color: %8; border: %2px solid %8; }
QPushButton#stopButton[outline="true"]:disabled { color: %6; border-color: %3; }
QFrame#advisorNote { background: rgba(168, 85, 247, 40); border: none; border-left: %10px solid #a855f7; border-radius: %11px; }
QLabel#advisorNoteTitle { color: #c084fc; font-weight: bold; }
QGroupBox#setpointBox { border: none; margin-top: 0; padding-top: 0; }
QCheckBox { color: %5; spacing: %12px; }
QCheckBox::indicator { width: %13px; height: %13px; }
)")
                  .arg(c.panel.name())
                  .arg(qMax(1, static_cast<int>(1 * ssf)))
                  .arg(c.border.name())
                  .arg(static_cast<int>(8 * ssf))
                  .arg(c.text.name(), c.muted.name(), c.active.name(), c.danger.name(), c.panelAlt.name())
                  .arg(static_cast<int>(3 * ssf))
                  .arg(static_cast<int>(5 * ssf))
                  .arg(static_cast<int>(8 * ssf))
                  .arg(static_cast<int>(18 * ssf)));
  for (QLabel *label : findChildren<QLabel *>()) {
    if (label->property("small").toBool()) {
      QFont small = base;
      small.setPointSizeF(base.pointSizeF() * 0.85);
      label->setFont(small);
    }
  }

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

void Manual::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  m_advisorNote->setVisible(m_resHolder->aiAdvisorEnabled);
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

  sendJob(m_model, tr("Manual setpoint"), QString{}, tr("Запуск работы по уставке"));
}

bool Manual::sendJob(ProgramModel *model, QString const &runLabel, QString const &testProfile, QString const &actionText) {
  if (m_resHolder->aiAdvisorEnabled && !offerOfflineAdvisorBeforeStart(m_resHolder, model, this)) {
    return false;
  }

  if (m_resHolder->recorder) {
    m_resHolder->recorder->setNextJob(ExperimentRecorder::JobInfo{testProfile.isEmpty() ? QStringLiteral("manual_setpoint")
                                                                                        : QStringLiteral("test_profile"),
                                                                  runLabel, testProfile, model->programVariant(), {}});
  }
  model->printToDebug();
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, model->makeSendData());
  m_resHolder->actions->addAction(ActionManager::ActionType::PLC, actionText);
  return true;
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

  setObjectName("setpointBox");
  if (alwaysEnable) {
    setTitle(QString());
  }
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(4 * ssf);
  auto muted = [this](QString const &text) {
    auto *label = new QLabel(text, this);
    label->setProperty("muted", true);
    return label;
  };
  auto unitInside = [ssf](QLineEdit *edit, QString const &unit) {
    // the unit inside the field, on the right
    auto *lay = new QHBoxLayout(edit);
    lay->setContentsMargins(0, 0, 8 * ssf, 0);
    lay->addStretch();
    auto *label = new QLabel(unit, edit);
    label->setProperty("muted", true);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    lay->addWidget(label);
    QFontMetrics const fm(label->font());
    edit->setTextMargins(0, 0, fm.horizontalAdvance(unit) + 14 * ssf, 0);
  };
  auto hint = [this, ssf](QString const &text) {
    auto *label = new QLabel(text, this);
    label->setProperty("muted", true);
    label->setProperty("small", true);
    label->setContentsMargins(28 * ssf, 0, 0, 0);
    label->setWordWrap(true);
    return label;
  };

  // current temperature
  mainLay->addWidget(muted(tr("Current temperature")));
  auto *currentRow = new QHBoxLayout();
  auto *current    = new LabelPlus(m_currentVal, this);
  QFont currentFont = QWidget::font();
  currentFont.setBold(true);
  currentFont.setPointSizeF(QWidget::font().pointSizeF() * 1.6);
  current->setFont(currentFont);
  auto *currentUnit = new QLabel(QStringLiteral("°C"), this);
  currentUnit->setFont(currentFont);
  currentRow->addWidget(current);
  currentRow->addWidget(currentUnit);
  currentRow->addStretch();
  mainLay->addLayout(currentRow);
  mainLay->addSpacing(10 * ssf);

  // new setpoint
  mainLay->addWidget(muted(tr("Новая уставка:").remove(':')));
  auto *setpoint = new LineEditNum(manualModel->getSysValue("temp"));
  QFont setpointFont = setpoint->font();
  setpointFont.setPointSizeF(setpointFont.pointSizeF() * 1.25);
  setpoint->setFont(setpointFont);
  setpoint->setFixedWidth(260 * ssf);
  unitInside(setpoint, QStringLiteral("°C"));
  mainLay->addWidget(setpoint);
  mainLay->addSpacing(12 * ssf);

  // ramp rate and hold time: a tick enables the field
  QString const speedUnitString = speedUnitIsHour ? tr("час") : tr("мин");
  auto         *tempSpeedSysValue = new SystemValue("temp_speed", 1, this);
  tempSpeedSysValue->setPrec(2);
  tempSpeedSysValue->setMin(0.01 * (speedUnitIsHour ? 60 : 1));
  tempSpeedSysValue->setMax(10 * (speedUnitIsHour ? 60 : 1));
  manualModel->add(tempSpeedSysValue);
  auto *holdTimeSysValue = new SystemValue("hold_time", 3600, this);
  holdTimeSysValue->setMin(60);
  holdTimeSysValue->setMax(3599999);
  manualModel->add(holdTimeSysValue);

  auto *rampCheck = new QCheckBox(tr("Ramp rate"), this);
  auto *rampEdit  = new LineEditNum(tempSpeedSysValue);
  rampEdit->setFixedWidth(200 * ssf);
  unitInside(rampEdit, QStringLiteral("°C/") + speedUnitString);
  auto *holdCheck = new QCheckBox(tr("Hold time"), this);
  auto *holdEdit  = new LineEditTime(holdTimeSysValue);
  auto *grid      = new QGridLayout();
  grid->setHorizontalSpacing(16 * ssf);
  grid->setVerticalSpacing(2 * ssf);
  grid->addWidget(rampCheck, 0, 0);
  grid->addWidget(rampEdit, 0, 1, Qt::AlignLeft);
  grid->addWidget(hint(tr("Without it the chamber goes to the setpoint as fast as it can.")), 1, 0, 1, 2);
  grid->setRowMinimumHeight(2, 10 * ssf);
  grid->addWidget(holdCheck, 3, 0);
  grid->addWidget(holdEdit, 3, 1, Qt::AlignLeft);
  grid->addWidget(hint(tr("Without it the setpoint is held until Stop.")), 4, 0, 1, 2);
  grid->setColumnStretch(2, 1);
  mainLay->addLayout(grid);

  rampEdit->setEnabled(false);
  holdEdit->setEnabled(false);
  connect(holdCheck, &QCheckBox::toggled, this, [flagsSysValue, holdEdit](bool value) {
    flagsSysValue->setBitOfUInt(TEMP_HOLD_CHECHED, value);
    holdEdit->setEnabled(value);
  });
  connect(rampCheck, &QCheckBox::toggled, this, [flagsSysValue, rampEdit](bool value) {
    flagsSysValue->setBitOfUInt(TEMP_RAMP_CHECHED, value);
    rampEdit->setEnabled(value);
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
