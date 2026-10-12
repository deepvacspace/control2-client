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

  m_startBtn = new QPushButton(tr("Start"), controlCard);
  m_startBtn->setObjectName("startButton");
  m_startDelayedBtn = new QPushButton(tr("Delay\nstart").replace('\n', ' '), controlCard);
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
  paramOrder.push_back({"temp", tr("Temperature, °C")});
  paramOrder.push_back({"hum", tr("Relative humidity, %")});
  paramOrder.push_back({"pres", tr("Pressure, mmHg")});
  paramOrder.push_back({"solar", tr("Solar lamp")});

  bool tempAlwaysEnable{true};
  if (m_modelSysValuesList->contains("pres")) {
    tempAlwaysEnable = false; // temperature and pressure are alternatives: each one is ticked
  }

  // temperature and pressure side by side, each one half of the card
  auto *pairLay = new QHBoxLayout();
  pairLay->setSpacing(20 * ssf);
  settingsLay->addLayout(pairLay);
  int paramCount{0U}; // number of added params

  for (int i{0U}; i < paramOrder.size(); ++i) {
    auto const &ckeckedParam = paramOrder[i];
    auto const &name         = ckeckedParam.first;
    QString     label        = ckeckedParam.second;

    bool const available{m_modelSysValuesList->contains(name)};
    if (!available && (name != "pres")) {
      continue;
    }
    int const   indexInSysValModel{static_cast<int>(m_modelSysValuesList->getNames().indexOf(name))};
    QString     unit;
    if (int const comma = static_cast<int>(label.lastIndexOf(',')); comma > 0) {
      unit  = label.mid(comma + 1).trimmed();
      label = label.left(comma).trimmed();
    }

    QGroupBox *newItem = nullptr;
    if (name == "temp") {
      newItem = new TempSetWidget(indexInSysValModel,
                                  m_modelSysValuesList,
                                  m_resHolder->states->getSysValue(name),
                                  false /*speedUnitIsHour*/,
                                  tempAlwaysEnable /*alwaysEnable*/,
                                  this);
    } else {
      // pressure is shown (disabled) also when the chamber has no pressure sensor
      SystemValue *current = m_resHolder->states->contains(name) ? m_resHolder->states->getSysValue(name) : nullptr;
      newItem = new CommonSetWidget(indexInSysValModel, available ? m_modelSysValuesList : nullptr, current, label,
                                    unit.isEmpty() && (name == "pres") ? tr("mmHg") : unit, this);
    }

    m_stateWidgets.insert(paramCount, newItem);
    if ((name == "temp") || (name == "pres")) {
      if ((name == "pres") && (pairLay->count() > 0)) {
        auto *line = new QFrame(controlCard);
        line->setObjectName("cardDivider");
        line->setFixedWidth(qMax(1, static_cast<int>(1 * ssf)));
        pairLay->addWidget(line);
      }
      pairLay->addWidget(newItem, 1);
    } else {
      settingsLay->addWidget(newItem);
    }
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
  noteLay->addWidget(noteTitle);
  controlLay->addWidget(m_advisorNote);
  controlLay->addStretch();

  // ---- right: details recorded with the next runs
  auto *detailsCard = makeCard(tr("Experiment details"), {});
  auto *detailsLay  = static_cast<QVBoxLayout *>(detailsCard->layout());
  detailsLay->addSpacing(4 * ssf);
  detailsLay->addWidget(new RunDetailsPanel(m_resHolder, detailsCard));
  detailsLay->addStretch();
  mainLay->addWidget(detailsCard, 1);

  setStyleSheet(QStringLiteral("QCheckBox { spacing: %1px; } QCheckBox::indicator { width: %2px; height: %2px; }")
                  .arg(static_cast<int>(8 * ssf))
                  .arg(static_cast<int>(18 * ssf)));
  QFont const base = QWidget::font();
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
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Chamber operation stopped by user"));
  });

  connect(m_startDelayedBtn, &QPushButton::clicked, this, [this] {
    auto *delaySelectDialog = new DelaySelectDialog(this);
    connect(delaySelectDialog, &DelaySelectDialog::startTimeSelected, this, [this](QDateTime startTime) {
      auto *delayInfoDialog = new DelayInfoDialog(tr("Setpoint operation"), startTime, this);
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

  sendJob(m_model, tr("Manual setpoint"), QString{}, tr("Starting setpoint operation"));
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

namespace {
// big value with its unit ("Current temperature")
QWidget *currentValueRow(SystemValue *value, QString const &unit, QWidget *parent) {
  auto *row    = new QWidget(parent);
  auto *lay    = new QHBoxLayout(row);
  lay->setContentsMargins(0, 0, 0, 0);
  QLabel *number = value ? static_cast<QLabel *>(new LabelPlus(value, row)) : new QLabel(QStringLiteral("—"), row);
  QFont   font   = parent->font();
  font.setBold(true);
  font.setPointSizeF(parent->font().pointSizeF() * 1.6);
  number->setFont(font);
  auto *unitLabel = new QLabel(unit, row);
  unitLabel->setFont(font);
  lay->addWidget(number);
  lay->addWidget(unitLabel);
  lay->addStretch();
  return row;
}

// unit inside a field, on the right
void unitInside(QLineEdit *edit, QString const &unit) {
  double const ssf{utils::ssf()};
  // the field fills its column (LineEditNum has a fixed width of its own)
  edit->setMinimumWidth(0);
  edit->setMaximumWidth(QWIDGETSIZE_MAX);
  auto        *lay = new QHBoxLayout(edit);
  lay->setContentsMargins(0, 0, 8 * ssf, 0);
  lay->addStretch();
  auto *label = new QLabel(unit, edit);
  label->setProperty("muted", true);
  label->setAttribute(Qt::WA_TransparentForMouseEvents);
  lay->addWidget(label);
  QFontMetrics const fm(label->font());
  edit->setTextMargins(0, 0, fm.horizontalAdvance(unit) + 14 * ssf, 0);
}

QLabel *mutedLabel(QString const &text, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  label->setProperty("muted", true);
  return label;
}

// column heading: plain, or a tick that switches the quantity on and off
QWidget *columnHeading(QString const &title, QCheckBox **tick, QWidget *parent) {
  if (tick) {
    *tick = new QCheckBox(title, parent);
    QFont font = (*tick)->font();
    font.setBold(true);
    (*tick)->setFont(font);
    return *tick;
  }
  auto *label = new QLabel(title, parent);
  label->setObjectName("cardTitle");
  return label;
}
} // namespace

TempSetWidget::TempSetWidget(quint32          index,
                             SystemValueList *manualModel,
                             SystemValue     *currentVal,
                             bool             speedUnitIsHour,
                             bool             alwaysEnable,
                             QWidget         *parent)
    : QGroupBox{parent}, m_modelSysValuesList{manualModel}, m_currentVal{currentVal} {
  auto *flagsSysValue = manualModel->getSysValue("flags");
  setObjectName("setpointBox");
  if (alwaysEnable) {
    flagsSysValue->setBitOfUInt(index, true); // NOTE important
  }
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(4 * ssf);
  mainLay->addWidget(columnHeading(tr("Temperature"), alwaysEnable ? nullptr : &m_enableChkBox, this));
  mainLay->addSpacing(6 * ssf);

  auto *content    = new QWidget(this);
  auto *contentLay = new QVBoxLayout(content);
  contentLay->setContentsMargins(0, 0, 0, 0);
  contentLay->setSpacing(4 * ssf);
  mainLay->addWidget(content);

  contentLay->addWidget(mutedLabel(tr("Current temperature"), content));
  contentLay->addWidget(currentValueRow(m_currentVal, QStringLiteral("°C"), content));
  contentLay->addSpacing(10 * ssf);

  // new setpoint: the whole column width
  contentLay->addWidget(mutedLabel(tr("New setpoint:").remove(':'), content));
  auto *setpoint     = new LineEditNum(manualModel->getSysValue("temp"));
  QFont setpointFont = setpoint->font();
  setpointFont.setPointSizeF(setpointFont.pointSizeF() * 1.25);
  setpoint->setFont(setpointFont);
  unitInside(setpoint, QStringLiteral("°C"));
  contentLay->addWidget(setpoint);
  contentLay->addSpacing(12 * ssf);

  // ramp rate and hold time: a tick enables the field
  QString const speedUnitString   = speedUnitIsHour ? tr("h") : tr("min");
  auto         *tempSpeedSysValue = new SystemValue("temp_speed", 1, this);
  tempSpeedSysValue->setPrec(2);
  tempSpeedSysValue->setMin(0.01 * (speedUnitIsHour ? 60 : 1));
  tempSpeedSysValue->setMax(10 * (speedUnitIsHour ? 60 : 1));
  manualModel->add(tempSpeedSysValue);
  auto *holdTimeSysValue = new SystemValue("hold_time", 3600, this);
  holdTimeSysValue->setMin(60);
  holdTimeSysValue->setMax(3599999);
  manualModel->add(holdTimeSysValue);

  auto *rampCheck = new QCheckBox(tr("Ramp rate"), content);
  auto *rampEdit  = new LineEditNum(tempSpeedSysValue);
  unitInside(rampEdit, QStringLiteral("°C/") + speedUnitString);
  auto *holdCheck = new QCheckBox(tr("Hold time"), content);
  auto *holdEdit  = new LineEditTime(holdTimeSysValue);
  auto *grid      = new QGridLayout();
  grid->setHorizontalSpacing(12 * ssf);
  grid->setVerticalSpacing(10 * ssf);
  grid->addWidget(rampCheck, 0, 0);
  grid->addWidget(rampEdit, 0, 1);
  grid->addWidget(holdCheck, 1, 0);
  grid->addWidget(holdEdit, 1, 1);
  grid->setColumnStretch(1, 1);
  contentLay->addLayout(grid);

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

  if (m_enableChkBox) {
    m_enableChkBox->setChecked(flagsSysValue->getBitOfUInt(index));
    content->setEnabled(m_enableChkBox->isChecked());
    connect(m_enableChkBox, &QCheckBox::toggled, this, [index, flagsSysValue, content](bool value) {
      flagsSysValue->setBitOfUInt(index, value);
      content->setEnabled(value);
    });
  }
}

CommonSetWidget::CommonSetWidget(quint32          index,
                                 SystemValueList *manualModel,
                                 SystemValue     *currentVal,
                                 QString const   &title,
                                 QString const   &unit,
                                 QWidget         *parent)
    : QGroupBox{parent}, m_modelSysValuesList{manualModel}, m_currentVal{currentVal} {
  setObjectName("setpointBox");
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(4 * ssf);
  QCheckBox *tick{nullptr};
  mainLay->addWidget(columnHeading(title, &tick, this));
  mainLay->addSpacing(6 * ssf);

  auto *content    = new QWidget(this);
  auto *contentLay = new QVBoxLayout(content);
  contentLay->setContentsMargins(0, 0, 0, 0);
  contentLay->setSpacing(4 * ssf);
  mainLay->addWidget(content);

  contentLay->addWidget(mutedLabel(tr("Current %1").arg(title.toLower()), content));
  contentLay->addWidget(currentValueRow(manualModel ? m_currentVal : nullptr, unit, content));
  contentLay->addSpacing(10 * ssf);

  contentLay->addWidget(mutedLabel(tr("New setpoint:").remove(':'), content));
  LineEditNum *setpoint = manualModel ? new LineEditNum(manualModel->getSysValue(manualModel->getNames().at(index)))
                                      : new LineEditNum(0, 1, 1, false, content);
  QFont setpointFont = setpoint->font();
  setpointFont.setPointSizeF(setpointFont.pointSizeF() * 1.25);
  setpoint->setFont(setpointFont);
  if (manualModel && manualModel->getSysValue(manualModel->getNames().at(index))->isSciNotation()) {
    // "7.50E-06" in the large digits does not fit half a card: smaller text, same field height as the temperature
    int const height = setpoint->sizeHint().height();
    QFont     sci    = QWidget::font();
    sci.setBold(true);
    sci.setPointSizeF(sci.pointSizeF() * 1.3);
    setpoint->setFont(sci);
    setpoint->setFixedHeight(height);
  }
  unitInside(setpoint, unit);
  contentLay->addWidget(setpoint);

  if (!manualModel) {
    // no sensor for this quantity on this chamber
    tick->setChecked(false);
    tick->setEnabled(false);
    content->setEnabled(false);
    setpoint->clear();
    mainLay->addSpacing(8 * ssf);
    mainLay->addWidget(mutedLabel(tr("No %1 sensor connected").arg(title.toLower()), this));
    return;
  }
  auto *flagsSysValue = manualModel->getSysValue("flags");
  tick->setChecked(flagsSysValue->getBitOfUInt(index));
  content->setEnabled(tick->isChecked());
  connect(tick, &QCheckBox::toggled, this, [index, flagsSysValue, content](bool value) {
    flagsSysValue->setBitOfUInt(index, value);
    content->setEnabled(value);
  });
}

} // namespace deepvaccontrol
