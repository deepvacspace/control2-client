#include "quicksetpoint.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

#include "constants.h"
#include "delayinfodialog.h"
#include "delayselectdialog.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "lineeditnum.h"
#include "lineedittime.h"
#include "offlineadvisorstart.h"
#include "programmodel.h"
#include "stopbutton.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
constexpr quint32 LONG_TIME{100 * 3600 - 1}; // held until stopped (as long as the PLC's timer allows)
// the ramp rates allowed, as in the program's custom ramps
constexpr double RATE_MIN{0.01};
constexpr double RATE_MAX{10.0};
}

QuickSetpointPanel::QuickSetpointPanel(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  double const       ssf{utils::ssf()};
  QVariantList const params = m_resHolder->settings->settingsMap().value("control_params").toList();
  m_model                   = new ProgramModel(params, ProgramModel::ProgType::SETPOINT, this);
  QStringList const order   = m_model->getParamListOrder();
  m_tempRow                 = static_cast<int>(order.indexOf(QStringLiteral("temp")));
  m_presRow                 = static_cast<int>(order.indexOf(QStringLiteral("pres")));

  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(12 * ssf);
  auto *grid = new QGridLayout();
  grid->setHorizontalSpacing(8 * ssf);
  grid->setVerticalSpacing(2 * ssf);
  // a title over its field, two columns
  int  field{0};
  auto place = [&](QWidget *title, QWidget *edit, int at) {
    if (title) {
      grid->addWidget(title, (at / 2) * 2, at % 2);
    }
    grid->addWidget(edit, (at / 2) * 2 + 1, at % 2);
    edit->setMinimumHeight(32 * ssf);
    edit->setMinimumWidth(0);
    edit->setMaximumWidth(QWIDGETSIZE_MAX);
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    edit->setFont(font()); // the page's font, not the large one of the screen inputs
    for (QWidget *part : edit->findChildren<QWidget *>()) {
      part->setFont(font());
    }
  };
  auto muted = [this](QString const &text) {
    auto *label = new QLabel(text, this);
    label->setProperty("muted", true);
    return label;
  };

  // variable
  m_variableCombo = new QComboBox(this);
  for (int row : {m_tempRow, m_presRow}) {
    if (row >= 0) {
      m_variableCombo->addItem(params.value(row).toMap().value("label").toString(), row);
    }
  }
  place(muted(tr("Variable")), m_variableCombo, field++);

  // setpoint: one edit per variable in the same cell, the variable's shown
  auto setpointEdit = [&](int row) -> LineEditNum * {
    if (row < 0) {
      return nullptr;
    }
    QVariantMap const param = params.value(row).toMap();
    auto *edit = new LineEditNum(param.value("min").toDouble(), param.value("max").toDouble(), param.value("prec", 1).toInt(), true, this);
    edit->setIsScientific(param.value("notation").toString() == QLatin1String("scientific"));
    edit->setText(edit->formatted(param.value("value").toDouble()));
    return edit;
  };
  m_setpointLabel = muted(tr("Setpoint"));
  m_tempEdit      = setpointEdit(m_tempRow);
  m_presEdit      = setpointEdit(m_presRow);
  bool titled{false};
  for (LineEditNum *edit : {m_tempEdit, m_presEdit}) {
    if (edit) {
      place(titled ? nullptr : m_setpointLabel, edit, field);
      titled = true;
    }
  }
  ++field;

  // optional: ramp rate (temperature) and hold time; a tick enables the field
  m_rampCheck = new QCheckBox(tr("Ramp rate, °C/min"), this);
  m_rateEdit  = new LineEditNum(RATE_MIN, RATE_MAX, 2, true, this);
  m_rateEdit->setText(m_rateEdit->formatted(1.0));
  place(m_rampCheck, m_rateEdit, field++);
  m_holdCheck = new QCheckBox(tr("Hold time"), this);
  m_holdEdit  = new LineEditTime(true, this);
  m_holdEdit->setTime(3600);
  m_holdCheck->setToolTip(tr("Without a hold time the setpoint is held until stopped"));
  place(m_holdCheck, m_holdEdit, field++);
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 1);
  lay->addLayout(grid);

  // Start, Delay start, Stop
  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(8 * ssf);
  m_startBtn = new QPushButton(tr("Start"), this);
  m_startBtn->setObjectName("startButton");
  m_startDelayedBtn = new QPushButton(tr("Delay\nstart").replace('\n', ' '), this);
  m_stopBtn         = new StopButton(this);
  for (QPushButton *button : {m_startBtn, m_startDelayedBtn, static_cast<QPushButton *>(m_stopBtn)}) {
    button->setFixedHeight(36 * ssf);
    button->setMinimumWidth(0);
    QFont bold = button->font();
    bold.setBold(true);
    bold.setPointSizeF(QWidget::font().pointSizeF() * 1.1);
    button->setFont(bold);
    buttons->addWidget(button, 1);
  }
  lay->addLayout(buttons);

  connect(m_variableCombo, &QComboBox::activated, this, &QuickSetpointPanel::updateGui);
  connect(m_rampCheck, &QCheckBox::toggled, this, &QuickSetpointPanel::updateGui);
  connect(m_holdCheck, &QCheckBox::toggled, this, &QuickSetpointPanel::updateGui);
  connect(m_startBtn, &QPushButton::clicked, this, &QuickSetpointPanel::start);
  connect(m_startDelayedBtn, &QPushButton::clicked, this, [this] {
    auto *delaySelectDialog = new DelaySelectDialog(this);
    connect(delaySelectDialog, &DelaySelectDialog::startTimeSelected, this, [this](QDateTime startTime) {
      auto *delayInfoDialog = new DelayInfoDialog(tr("Setpoint operation"), startTime, this);
      connect(delayInfoDialog, &QDialog::accepted, this, &QuickSetpointPanel::start);
      delayInfoDialog->open();
    });
    delaySelectDialog->open();
  });
  // the job, whatever started it
  connect(m_stopBtn, &StopButton::confirmPressed, this, [this] {
    m_resHolder->tcpGate->sendTextCmd("stop");
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Chamber operation stopped by user"));
  });
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &QuickSetpointPanel::updateGui);
  connect(m_resHolder->users, &AccessManager::userSelected, this, &QuickSetpointPanel::updateGui);
  updateGui();
}

QString QuickSetpointPanel::pressureUnit() const {
  QString const label = m_resHolder->states->contains(QStringLiteral("pres"))
                          ? m_resHolder->states->getSysValue(QStringLiteral("pres"))->getLabel()
                          : QString{};
  int const comma{static_cast<int>(label.lastIndexOf(QLatin1Char(',')))};
  return (comma > 0) ? label.mid(comma + 1).trimmed() : QStringLiteral("Torr");
}

void QuickSetpointPanel::updateGui() {
  bool const pressure{(m_presRow >= 0) && (m_variableCombo->currentData().toInt() == m_presRow)};
  m_variableCombo->setEnabled(m_variableCombo->count() > 1);
  if (m_tempEdit) {
    m_tempEdit->setVisible(!pressure);
  }
  if (m_presEdit) {
    m_presEdit->setVisible(pressure);
  }
  m_setpointLabel->setText(pressure ? tr("Setpoint, %1").arg(pressureUnit()) : tr("Setpoint, °C"));
  // the PLC ramps temperature only; a pressure's hold time counts from the start (it waits for no target)
  m_rampCheck->setEnabled(!pressure);
  m_rateEdit->setEnabled(!pressure && m_rampCheck->isChecked());
  m_holdCheck->setText(pressure ? tr("Duration") : tr("Hold time"));
  m_holdEdit->setEnabled(m_holdCheck->isChecked());

  // as Manual control: Start also while a job runs (a setpoint is replaced; a program: stop it first), Delay start
  // when idle or finished; nothing for an Observer
  auto const status = m_resHolder->hard_status->getHardwareStatus();
  bool const allowed{AccessManager::AccessLevel::SPECTATOR_0 != m_resHolder->users->getCurrentAccessLevel()};
  bool const idle{(HardwareStatus::HardwareStatusEnum::IDLE == status) || (HardwareStatus::HardwareStatusEnum::FINISH == status)};
  bool const working{HardwareStatus::HardwareStatusEnum::WORK == status};
  m_startBtn->setEnabled(allowed && (idle || working));
  m_startDelayedBtn->setEnabled(allowed && idle);
  m_stopBtn->setEnabled(allowed && working);
}

bool QuickSetpointPanel::buildJob() {
  bool const   pressure{(m_presRow >= 0) && (m_variableCombo->currentData().toInt() == m_presRow)};
  int const    row{pressure ? m_presRow : m_tempRow};
  LineEditNum *edit = pressure ? m_presEdit : m_tempEdit;
  bool         ok{false};
  double const target = edit ? edit->number(&ok) : 0.0;
  if ((row < 0) || !ok) {
    (new MessageDialog(tr("Quick setpoint"), tr("Give the setpoint."), this))->open();
    return false;
  }
  bool const ramp{!pressure && m_rampCheck->isChecked()};
  bool const hold{m_holdCheck->isChecked()};

  // every parameter at its default, the variable's at the setpoint
  QStringList const  order  = m_model->getParamListOrder();
  QVariantList const params = m_model->getParamListVariant();
  QVariantMap        base;
  for (int i{0}; i < order.size(); ++i) {
    base[order[i]] = params.value(i).toMap().value("value");
  }
  base[order[row]] = target;
  quint32 bits{0U};
  utils::setBitOfDword(bits, row, true);
  m_model->getStandardModel()->removeColumns(0, m_model->getStandardModel()->columnCount());

  // a ramp at the rate from the chamber's temperature now, then the setpoint held
  if (ramp) {
    SystemValue *temp = m_resHolder->states->contains(QStringLiteral("temp")) ? m_resHolder->states->getSysValue(QStringLiteral("temp")) : nullptr;
    bool const   online{StatusManager::AppState::ONLINE == m_resHolder->status->getAppState()};
    bool         rateOk{false};
    double const rate = qBound(RATE_MIN, m_rateEdit->number(&rateOk), RATE_MAX);
    if (!temp || !online || !std::isfinite(temp->getValue()) || !rateOk) {
      (new MessageDialog(tr("Quick setpoint"), tr("A ramp needs the chamber's temperature and a ramp rate."), this))->open();
      return false;
    }
    QVariantMap interval = base;
    quint32     flags{bits};
    utils::setBitOfDword(flags, RAMP_BIT, true);
    interval["duration"] = static_cast<quint32>(std::lround(std::abs(temp->getValue() - target) / rate * 60.0));
    interval["flags"]    = flags;
    m_model->setIntervalFromVariantMap(0, interval);
  }
  QVariantMap interval = base;
  quint32     flags{bits};
  utils::setBitOfDword(flags, AWAIT_BIT, hold && !pressure); // the hold starts at the target (temperature only)
  interval["duration"] = hold ? static_cast<quint32>(m_holdEdit->getSeconds()) : LONG_TIME;
  interval["flags"]    = flags;
  m_model->setIntervalFromVariantMap(m_model->getStandardModel()->columnCount(), interval);
  return true;
}

void QuickSetpointPanel::start() {
  // a program is not replaced: it is stopped first (a setpoint running is replaced, as Manual control did)
  if ((HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus())
      && m_resHolder->states->contains("work_type")
      && (static_cast<int>(m_resHolder->states->getSysValue("work_type")->getValue()) != static_cast<int>(ProgramModel::ProgType::SETPOINT))) {
    (new MessageDialog(tr("Quick setpoint"), tr("A program is running. Stop it before starting a quick setpoint."), this))->open();
    return;
  }
  if (!buildJob()) {
    return;
  }
  if (m_resHolder->aiAdvisorEnabled && !offerOfflineAdvisorBeforeStart(m_resHolder, m_model, this)) {
    return;
  }
  if (m_resHolder->recorder) {
    m_resHolder->recorder->setNextJob(
      ExperimentRecorder::JobInfo{QStringLiteral("manual_setpoint"), tr("Quick setpoint"), QString{}, m_model->programVariant(), {}});
  }
  m_model->printToDebug();
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, m_model->makeSendData());
  m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Starting setpoint operation"));
}

}
