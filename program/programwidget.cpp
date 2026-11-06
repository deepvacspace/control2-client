#include "programwidget.h"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

#include "programstepper.h"
#include "quicksetpoint.h"
#include "constants.h"
#include "delayinfodialog.h"
#include "delayselectdialog.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "rundetailsdialog.h"
#include "intervalcardlist.h"
#include "intervaleditdialog.h"
#include "advisorsafety.h"
#include "offlineadvisorstart.h"
#include "programplanrunner.h"
#include "settingsmanager.h"
#include "statusmanager.h"
#include "systemvalue.h"
#include "systemvaluelist.h"
#include "programlibrarydialog.h"
#include "stopbutton.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
// the steps on the left
enum Step { STEP_DETAILS = 0, STEP_INTERVALS = 1, STEP_START = 2 };

constexpr int    FORECAST_DELAY_MS{800};  // after the last edit
constexpr double FORECAST_TEMP_STEP_C{2.0}; // the chamber's temperature moved this much: estimated again

QString durationText(qint64 total) {
  return (total >= 3600) ? QObject::tr("%1 h %2 min").arg(total / 3600).arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
                         : QObject::tr("%1 min").arg((total + 59) / 60);
}

// the chamber's temperature now: none while not connected
std::optional<double> chamberTemperature(ResHolder *resHolder) {
  bool const   online{resHolder->status && (StatusManager::AppState::ONLINE == resHolder->status->getAppState())};
  SystemValue *value = resHolder->states->getSysValue("temp");
  if (!online || !value || !std::isfinite(value->getValue())) {
    return std::nullopt;
  }
  return value->getValue();
}

// the program's name: as wide as there is room for, cut with "…" (the whole name in the tool tip)
class ElidedLabel : public QLabel {
public:
  using QLabel::QLabel;
  void setFullText(QString const &text) {
    m_full = text;
    setToolTip(text);
    updateGeometry();
    elide();
  }
  [[nodiscard]] QSize minimumSizeHint() const override {
    return {0, QLabel::minimumSizeHint().height()};
  }
  [[nodiscard]] QSize sizeHint() const override {
    QFontMetrics const metrics = fontMetrics();
    return {metrics.horizontalAdvance(m_full) + metrics.averageCharWidth() + contentsMargins().left() + contentsMargins().right(),
            QLabel::sizeHint().height()};
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QLabel::resizeEvent(event);
    elide();
  }
  // the style sheet's font comes after the text: measured again
  void changeEvent(QEvent *event) override {
    QLabel::changeEvent(event);
    if ((event->type() == QEvent::FontChange) || (event->type() == QEvent::StyleChange)) {
      updateGeometry();
      elide();
    }
  }

private:
  void elide() {
    QString const shown = fontMetrics().elidedText(m_full, Qt::ElideRight, contentsRect().width());
    if (shown != text()) {
      setText(shown);
    }
  }
  QString m_full;
};
}
ProgramWidget::ProgramWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}
    , m_resHolder{resHolder}
    , m_model{nullptr}
    , m_accessLevel{resHolder->users->getCurrentAccessLevel()} {
  m_forecaster = new ai::ProgramForecaster(this);
  m_planRunner = new ai::ProgramPlanRunner(m_resHolder, this);
  m_forecastTimer.setSingleShot(true);
  m_forecastTimer.setInterval(FORECAST_DELAY_MS);
  connect(&m_forecastTimer, &QTimer::timeout, this, &ProgramWidget::runForecast);
  connect(m_forecaster, &ai::ProgramForecaster::finished, this, &ProgramWidget::showForecast);
  // a plan left on the chamber by a crash: the user's own settings are back, the user decides about the job
  ai::ManualPidGuard *guard = m_planRunner->guard();
  connect(guard, &ai::ManualPidGuard::recoveredPreviousSession, this, [this, guard](QString const &reason, bool jobMayBeRunning) {
    ai::notifyRecoveredSession(this, guard, reason, jobMayBeRunning);
  });
  connect(guard, &ai::ManualPidGuard::restoreFailed, this,
          [](QString const &message) { (new MessageDialog(tr("AI Advisor"), message))->open(); });

  initGui();
  setProgramModel(resHolder->defaultProgram);
  updateGui();

  // a new interval after the selected one (a copy of it)
  connect(m_newBtn, &QPushButton::clicked, this, [this]() {
    if (m_model) {
      int const nexIndex{m_model->getSelectedInterval() + 1};
      if (nexIndex <= m_labels.size()) {
        m_labels.insert(nexIndex, QString());
        labelsEdited();
      }
      m_model->newItem(nexIndex);
      m_model->selectInterval(nexIndex);
    }
  });
  connect(m_intervalList, &IntervalCardList::duplicateRequested, this, [this](int interval) {
    if (m_model) {
      if (interval < m_labels.size()) {
        m_labels.insert(interval + 1, m_labels.value(interval));
        labelsEdited();
      }
      m_model->newItem(interval + 1); // a copy of the one before it
      m_model->selectInterval(interval + 1);
    }
  });
  connect(m_intervalList, &IntervalCardList::deleteRequested, this, [this](int interval) {
    if (m_model) {
      if (1 == m_model->getStandardModel()->columnCount()) {
        return;
      }
      if (interval < m_labels.size()) {
        m_labels.removeAt(interval);
        labelsEdited();
      }
      m_model->delItem(interval);
    }
  });
  connect(m_intervalList, &IntervalCardList::labelEdited, this, [this](int interval, QString const &label) {
    while (m_labels.size() <= interval) {
      m_labels.append(QString());
    }
    m_labels[interval] = label;
    labelsEdited();
  });

  // a hold of a ramp's target right after it
  connect(m_intervalList, &IntervalCardList::holdRequested, this, [this](int interval) {
    if (m_model) {
      if (interval < m_labels.size()) {
        m_labels.insert(interval + 1, QString());
        labelsEdited();
      }
      m_model->newItem(interval + 1); // a copy of the ramp: its target
      m_intervalList->makeHold(interval + 1, 3600);
      m_model->selectInterval(interval + 1);
    }
  });

  connect(m_startBtn, &QPushButton::clicked, this, [this]() {
    if (m_model) {
      sendProgram();
    }
  });

  connect(m_startDelayedBtn, &QPushButton::clicked, this, [this] {
    if (m_model && readyToStart()) {
      auto *delaySelectDialog = new DelaySelectDialog(this);
      connect(delaySelectDialog, &DelaySelectDialog::startTimeSelected, this, [this](QDateTime startTime) {
        auto *delayInfoDialog = new DelayInfoDialog(tr("Program operation"), startTime, this);
        connect(delayInfoDialog, &QDialog::accepted, this, &ProgramWidget::sendProgram);
        delayInfoDialog->open();
      });
      delaySelectDialog->open();
    }
  });

  // the job, whatever started it
  connect(m_stopBtn, &StopButton::confirmPressed, this, [this]() {
    m_resHolder->tcpGate->sendTextCmd("stop");
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Chamber operation stopped by user"));
  });

  connect(m_programGraph, &ProgramGraph::intervalDoubleClicked, this, &ProgramWidget::openIntervalEditDialog);

  connect(m_libraryBtn, &QAbstractButton::clicked, this, &ProgramWidget::openLibrary);
  connect(m_saveLibraryBtn, &QAbstractButton::clicked, this, &ProgramWidget::saveToLibrary);

  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &ProgramWidget::updateGui);
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &ProgramWidget::updateRunStatus);
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &ProgramWidget::updateRunStatus);
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &ProgramWidget::temperatureMoved);
  // the first interval ramps from the chamber's temperature: its ramp rate and the profile drawn with it
  auto updateStartTemperature = [this] {
    std::optional<double> const temperature = chamberTemperature(m_resHolder);
    m_intervalList->setStartTemperature(temperature);
    m_programGraph->setStartTemperature(temperature);
  };
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, updateStartTemperature);
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, updateStartTemperature);
  updateStartTemperature();
  // the estimate uses the chamber's settings; after a job it starts from where the chamber is then
  connect(m_resHolder->settings, &SettingsManager::plcSettingsChanged, this, [this] { m_forecastTimer.start(); });
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, [this] { m_forecastTimer.start(); });
  connect(m_resHolder->users, &AccessManager::userSelected, this, &ProgramWidget::handleAccessChange);
  // "ends 19:50 if started now" follows the clock
  m_summaryTimer.setInterval(30'000);
  connect(&m_summaryTimer, &QTimer::timeout, this, &ProgramWidget::updateSequenceInfo);
  m_summaryTimer.start();
  updateProgramLabel();
  updateRunStatus();
  updateSequenceInfo();
  updateConfirmation();
  updateButtonIcons();
  programEdited();
}

void ProgramWidget::setProgramModel(ProgramModel *newProgramModel) {
  if (newProgramModel == m_model) {
    return;
  }

  if (m_model) {
    disconnect(m_model, nullptr, nullptr, nullptr);
  }

  m_model = newProgramModel;
  m_intervalList->setModel(newProgramModel);
  if (m_resHolder->states->contains(QStringLiteral("pres"))) { // a programmed pressure is in the chamber's unit
    QString const pressureLabel = m_resHolder->states->getSysValue(QStringLiteral("pres"))->getLabel();
    m_programGraph->setPressureLabel(pressureLabel);
    m_intervalList->setPressureLabel(pressureLabel);
  }
  m_programGraph->setModel(newProgramModel);
  m_cycleWidget->setParams(m_model->getCycleParams());
  m_cycleWidget->setIntervalsCount(m_model->getStandardModel()->columnCount());

  connect(m_model, &ProgramModel::numberOfIntervalChanged, this, &ProgramWidget::updateGui);
  // edits after opening a library program mark it as modified
  auto markModified = [this] {
    if (!m_loadingLibrary && !m_libraryName.isEmpty() && !m_libraryModified) {
      m_libraryModified = true;
      updateProgramLabel();
    }
  };
  connect(m_model, &ProgramModel::numberOfIntervalChanged, this, markModified);
  connect(m_model, &ProgramModel::cycleParamsUpdated, this, markModified);
  connect(m_model, &ProgramModel::modelChanged, this, markModified);
  connect(m_model, &ProgramModel::cycleParamsUpdated, m_cycleWidget, &CycleWidget::setParams);
  connect(m_model, &ProgramModel::numberOfIntervalChanged, this, &ProgramWidget::programEdited);
  connect(m_model, &ProgramModel::modelChanged, this, &ProgramWidget::programEdited);
  connect(m_model->getStandardModel(), &QStandardItemModel::dataChanged, this, &ProgramWidget::programEdited);
  connect(m_model, &ProgramModel::cycleParamsUpdated, this, &ProgramWidget::programEdited);
  connect(m_model, &ProgramModel::numberOfIntervalChanged, this, &ProgramWidget::updateSequenceInfo);
  connect(m_model, &ProgramModel::modelChanged, this, &ProgramWidget::updateSequenceInfo);
  connect(m_model->getStandardModel(), &QStandardItemModel::dataChanged, this, &ProgramWidget::updateSequenceInfo);
  connect(m_cycleWidget, &CycleWidget::paramsChanged, m_model, &ProgramModel::setCycleParams);
  connect(m_cycleWidget, &CycleWidget::paramsChanged, this, [this] {
    programEdited();
    updateSequenceInfo();
    updateConfirmation();
  });
  if (m_forecaster) {
    programEdited();
  }
}

void ProgramWidget::openIntervalEditDialog() {
  auto *editDialog = new IntervalEditDialog(m_model, &m_labels, this);
  connect(editDialog, &IntervalEditDialog::labelsChanged, this, &ProgramWidget::labelsEdited);
  editDialog->open();
}

void ProgramWidget::initGui() {
  double const ssf{utils::ssf()};
  auto        *root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(10 * ssf);
  // ---- left: the steps
  // the program in steps, or a quick setpoint; a third of the width (resizeEvent)
  m_leftTabs = new QTabWidget(this);
  theme::setFlatTabs(m_leftTabs);
  m_stepper = new ProgramStepper(m_leftTabs);
  m_leftTabs->addTab(m_stepper, tr("Program"));
  {
    auto *quickPage = new QWidget(m_leftTabs);
    auto *quickLay  = new QVBoxLayout(quickPage);
    quickLay->setContentsMargins(0, 8 * ssf, 0, 0);
    auto *quickCard = new QFrame(quickPage);
    quickCard->setObjectName("pageCard");
    auto *cardLay = new QVBoxLayout(quickCard);
    cardLay->setContentsMargins(14 * ssf, 12 * ssf, 14 * ssf, 12 * ssf);
    cardLay->addWidget(new QuickSetpointPanel(m_resHolder, quickCard));
    quickLay->addWidget(quickCard);
    quickLay->addStretch();
    m_leftTabs->addTab(quickPage, tr("Quick setpoint"));
  }
  root->addWidget(m_leftTabs);
  auto *column = new QVBoxLayout();
  column->setSpacing(10 * ssf);
  root->addLayout(column, 1);

  auto makeCard = [this, ssf](QBoxLayout *lay) {
    auto *card = new QFrame(this);
    card->setObjectName("pageCard");
    card->setLayout(lay);
    lay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 10 * ssf);
    lay->setSpacing(8 * ssf);
    return card;
  };
  auto muted = [this](QString const &text) {
    auto *label = new QLabel(text, this);
    label->setProperty("muted", true);
    return label;
  };
  auto bold = [](QLabel *label) {
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
  };

  // ---- the profile: the program's name (a library program: its badge), how long it lasts and when it would end
  auto *graphLay = new QVBoxLayout();
  auto *graphCard = makeCard(graphLay);
  auto *titleRow  = new QHBoxLayout();
  titleRow->setSpacing(10 * ssf);
  m_programLabel = bold(new ElidedLabel(this));
  m_libraryBadge = new QLabel(this);
  m_summaryLabel = muted(QString{});
  titleRow->addWidget(m_programLabel);
  titleRow->addWidget(m_libraryBadge);
  titleRow->addSpacing(6 * ssf);
  titleRow->addWidget(m_summaryLabel);
  titleRow->addStretch();
  graphLay->addLayout(titleRow);
  m_programGraph = new ProgramGraph(graphCard);
  graphLay->addWidget(m_programGraph, 1);
  column->addWidget(graphCard, 1);

  // ---- step 1: the experiment details, recorded with it
  auto *detailsPage = new QWidget(this);
  auto *detailsLay  = new QVBoxLayout(detailsPage);
  detailsLay->setContentsMargins(0, 0, 0, 0);
  detailsLay->setSpacing(8 * ssf);
  m_details = new RunDetailsPanel(m_resHolder, detailsPage);
  m_details->setRequiredFields(); // the operator is the logged-in user unless typed otherwise
  detailsLay->addWidget(m_details);
  m_stepper->addStep(tr("Experiment details"), detailsPage);

  // ---- step 2: the program library, the intervals and the cycle
  auto *intervalsPage = new QWidget(this);
  auto *intervalsLay  = new QVBoxLayout(intervalsPage);
  intervalsLay->setContentsMargins(0, 0, 0, 0);
  intervalsLay->setSpacing(8 * ssf);
  {
    auto *libraryRow = new QHBoxLayout();
    libraryRow->setSpacing(8 * ssf);
    auto *loadBtn = new QPushButton(tr("Load from library"), intervalsPage);
    loadBtn->setToolTip(tr("Load a program from the program library (also those shared by paired computers)"));
    auto *saveBtn = new QPushButton(tr("Save to library"), intervalsPage);
    saveBtn->setToolTip(tr("Save this program in the program library, by name"));
    for (QPushButton *button : {loadBtn, saveBtn}) {
      button->setFixedHeight(30 * ssf);
      button->setFocusPolicy(Qt::NoFocus);
      libraryRow->addWidget(button, 1);
    }
    m_libraryBtn     = loadBtn;
    m_saveLibraryBtn = saveBtn;
    intervalsLay->addLayout(libraryRow);
  }
  m_intervalList = new IntervalCardList(intervalsPage);
  intervalsLay->addWidget(m_intervalList);
  // under the last interval: the cycle (a range of intervals repeated)
  {
    auto *cycleBox = new QFrame(intervalsPage);
    cycleBox->setObjectName("pageCard");
    auto *cycleLay = new QVBoxLayout(cycleBox);
    cycleLay->setContentsMargins(12 * ssf, 8 * ssf, 12 * ssf, 10 * ssf);
    cycleLay->setSpacing(4 * ssf);
    QLabel *cycleTitle = bold(new QLabel(tr("Cycle"), cycleBox));
    QLabel *cycleNote  = muted(tr("Repeat a range of intervals a number of times."));
    cycleNote->setWordWrap(true);
    m_cycleWidget = new CycleWidget(cycleBox);
    cycleLay->addWidget(cycleTitle);
    cycleLay->addWidget(cycleNote);
    cycleLay->addWidget(m_cycleWidget);
    intervalsLay->addWidget(cycleBox);
  }
  m_stepper->addStep(tr("Intervals"), intervalsPage);
  m_newBtn = new QPushButton(tr("Add"), this);
  m_newBtn->setObjectName("primaryButton");
  m_newBtn->setToolTip(tr("A new interval after the selected one"));
  m_newBtn->setFixedHeight(28 * ssf);
  m_newBtn->setFocusPolicy(Qt::NoFocus);
  m_stepper->setHeaderWidget(STEP_INTERVALS, m_newBtn);

  // ---- step 3: the experiment as it will start; Start, Delay start, Stop
  auto *startPage = new QWidget(this);
  auto *startLay  = new QVBoxLayout(startPage);
  startLay->setContentsMargins(0, 0, 0, 0);
  startLay->setSpacing(10 * ssf);
  auto *facts = new QGridLayout();
  facts->setHorizontalSpacing(12 * ssf);
  facts->setVerticalSpacing(6 * ssf);
  int  factRow{0};
  auto fact   = [&](QString const &title, QLabel *&value) {
    value = bold(new QLabel(startPage));
    value->setWordWrap(true);
    facts->addWidget(muted(title), factRow, 0, Qt::AlignTop | Qt::AlignLeft);
    facts->addWidget(value, factRow, 1);
    ++factRow;
  };
  fact(tr("Name or purpose"), m_purposeValue);
  fact(tr("Device under test"), m_dutValue);
  fact(tr("Operator"), m_operatorValue);
  fact(tr("Program"), m_programValue);
  fact(tr("Cycle"), m_cycleValue);
  fact(tr("Duration"), m_durationValue);
  facts->setColumnStretch(1, 1);
  startLay->addLayout(facts);
  m_runStatus = new QLabel(startPage);
  m_runStatus->setWordWrap(true);
  startLay->addWidget(m_runStatus);
  auto *buttonsLay = new QHBoxLayout();
  buttonsLay->setSpacing(8 * ssf);
  m_startBtn = new QPushButton(tr("Start"), startPage);
  m_startBtn->setObjectName("startButton");
  m_startDelayedBtn = new QPushButton(tr("Delay\nstart").replace('\n', ' '), startPage);
  m_stopBtn         = new StopButton(startPage);
  for (QPushButton *btn : {m_startBtn, m_startDelayedBtn, static_cast<QPushButton *>(m_stopBtn)}) {
    btn->setFixedHeight(36 * ssf);
    btn->setMinimumWidth(0);
    QFont font = btn->font();
    font.setBold(true);
    font.setPointSizeF(QWidget::font().pointSizeF() * 1.1);
    btn->setFont(font);
    buttonsLay->addWidget(btn, 1);
  }
  startLay->addLayout(buttonsLay);
  m_stepper->addStep(tr("Confirm and start"), startPage);

  // Next: step 1 needs its required fields, step 2 a setpoint in every interval
  auto updateDetailsNext = [this] { m_stepper->setNextEnabled(STEP_DETAILS, detailsComplete()); };
  connect(m_details, &RunDetailsPanel::edited, this, updateDetailsNext);
  updateDetailsNext();
  connect(m_stepper, &ProgramStepper::nextRequested, this, [this](int step) {
    if ((step == STEP_DETAILS) && detailsComplete()) {
      m_stepper->setCurrent(STEP_INTERVALS);
    } else if ((step == STEP_INTERVALS) && intervalsReady()) {
      m_stepper->setCurrent(STEP_START);
    }
  });

  if (m_resHolder->recorder) {
    connect(m_resHolder->recorder, &ExperimentRecorder::nextRunMetadataChanged, this, &ProgramWidget::updateConfirmation);
  }
}

void ProgramWidget::handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel) {
  if (newLevel == m_accessLevel) {
    return;
  }

  m_accessLevel = newLevel;
  updateGui();
}

void ProgramWidget::updateGui() {
  int const intervalCount{m_model->getStandardModel()->columnCount()};

  HardwareStatus::HardwareStatusEnum const hardWorkStatus{m_resHolder->hard_status->getHardwareStatus()};
  bool const working{HardwareStatus::HardwareStatus::WORK == hardWorkStatus};

  // not for an Observer; idle or finished (while a job runs, a tap says to stop it first: readyToStart())
  bool const enableStart{((HardwareStatus::HardwareStatusEnum::IDLE == hardWorkStatus)
                          || (HardwareStatus::HardwareStatusEnum::FINISH == hardWorkStatus) || working)
                         && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel) && (intervalCount > 0)};
  m_startBtn->setEnabled(enableStart);
  m_startDelayedBtn->setEnabled(enableStart);
  m_stopBtn->setEnabled(working && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel));
  // while a job runs: step 3 only (its Stop); back to the other steps once the chamber is stopped
  m_stepper->setLocked(working);
  if (working) {
    m_stepper->setCurrent(STEP_START);
  }

  m_cycleWidget->setIntervalsCount(intervalCount);

  bool const editable{m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2};
  m_saveLibraryBtn->setEnabled(editable && (intervalCount > 0));
  m_newBtn->setEnabled(editable);
  m_intervalList->setEditable(editable);
  m_cycleWidget->setEnabled(editable && (intervalCount > 0)); // one interval too: repeated, or held until stopped
}

bool ProgramWidget::readyToStart() {
  if (HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus()) {
    (new MessageDialog(tr("Program control"), tr("A job is running. Stop it before starting a program."), this))->open();
    return false;
  }
  if (!detailsComplete()) {
    m_stepper->setCurrent(STEP_DETAILS);
    (new MessageDialog(tr("Program control"), tr("Give the experiment a name or purpose and an operator."), this))->open();
    return false;
  }
  return intervalsReady();
}

bool ProgramWidget::detailsComplete() const {
  return m_details && m_details->complete();
}

int ProgramWidget::intervalWithoutSetpoint() const {
  // every interval controls something: its temperature or pressure (or another quantity of the chamber)
  QStringList const  order  = m_model->getParamListOrder();
  QVariantList const params = m_model->getParamListVariant();
  int const          flagsRow{m_model->getFlagsIndex()};
  for (int interval{0}; interval < m_model->getStandardModel()->columnCount(); ++interval) {
    quint32 const flags{m_model->getStandardModel()->item(flagsRow, interval)->data(Qt::UserRole + 1).toUInt()};
    bool          controlled{false};
    for (int row{0}; row < order.size(); ++row) {
      controlled = controlled || ((params.value(row).toMap().value("type").toInt() == 0) && utils::getBitOfDword(flags, row));
    }
    if (!controlled) {
      return interval;
    }
  }
  return -1;
}

bool ProgramWidget::intervalsReady() {
  int const interval{intervalWithoutSetpoint()};
  if (interval < 0) {
    return true;
  }
  m_stepper->setCurrent(STEP_INTERVALS);
  m_intervalList->showInterval(interval);
  (new MessageDialog(tr("Program control"),
                     tr("Interval %1 has no setpoint. Choose its variable and give it a setpoint.").arg(interval + 1), this))
    ->open();
  return false;
}

void ProgramWidget::sendProgram() {
  if (!readyToStart()) { // also when a delayed start comes: a job may have been started meanwhile
    return;
  }

  // a custom ramp in the first interval: its time from the chamber's temperature now (also after a delayed start)
  m_intervalList->setStartTemperature(chamberTemperature(m_resHolder));
  m_intervalList->prepareForStart();

  QVector<ai::IntervalPlan> plan;
  if (m_resHolder->aiAdvisorEnabled && !offerProgramPlanBeforeStart(m_resHolder, m_model, this, &plan, m_forecaster->plan())) {
    return;
  }
  if (!plan.isEmpty()) {
    QString error;
    if (!m_planRunner->start(plan, &error)) {
      (new MessageDialog(tr("AI Advisor"), error + tr("\n\nThe experiment was not started.")))->open();
      return;
    }
  }

  m_model->printToDebug();
  m_runningName   = m_libraryName.isEmpty() ? QString{} : (m_libraryModified ? tr("%1 (modified)").arg(m_libraryName) : m_libraryName);
  m_runningLabels = m_labels;
  if (m_resHolder->recorder) {
    m_resHolder->recorder->setNextJob(ExperimentRecorder::JobInfo{
      QStringLiteral("program"), m_runningName.isEmpty() ? tr("Program") : tr("Program %1").arg(m_runningName), m_runningName,
      m_model->programVariant(), QJsonObject{{"labels", QJsonArray::fromStringList(m_runningLabels)}}});
  }
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, m_model->makeSendData());
  QString const actionMsg{
    QString(tr("Starting program operation. %1 interval(s).")).arg(m_model->getStandardModel()->columnCount())};
  m_resHolder->actions->addAction(ActionManager::ActionType::PLC,
                                  m_runningName.isEmpty() ? actionMsg : QStringLiteral("%1 (%2)").arg(actionMsg, m_runningName));
  updateRunStatus();
}

void ProgramWidget::openLibrary() {
  auto *dialog = new ProgramLibraryDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog] {
    auto const profile = dialog->selected();
    if (!profile || !m_model) {
      return;
    }
    QString error;
    m_loadingLibrary = true;
    bool const ok    = programlibrary::loadIntoModel(*profile, *m_model, &error);
    m_loadingLibrary = false;
    if (!ok) {
      (new MessageDialog(tr("Program library"), error))->open();
      return;
    }
    QStringList labels;
    for (auto const &step : profile->steps) {
      labels << step.label;
    }
    setLibraryProgram(profile->id, profile->name, labels);
    m_model->selectInterval(0);
  });
  dialog->open();
}

void ProgramWidget::saveToLibrary() {
  if (!m_model || (m_model->getStandardModel()->columnCount() < 1)) {
    return;
  }
  auto *dialog = new ProgramSaveDialog(*m_model, m_libraryName, m_labels, m_resHolder->users->getCurrentUserName(), this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &QDialog::accepted, this, [this, dialog] {
    if (auto const profile = dialog->saved()) {
      QStringList labels;
      for (auto const &step : profile->steps) {
        labels << step.label;
      }
      setLibraryProgram(profile->id, profile->name, labels);
    }
  });
  dialog->open();
}

void ProgramWidget::setLibraryProgram(qint64 id, QString const &name, QStringList const &labels) {
  m_libraryId       = id;
  m_libraryName     = name;
  m_labels          = labels;
  m_libraryModified = false;
  updateProgramLabel();
}

void ProgramWidget::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    updateProgramLabel(); // badge colours follow the theme
    updateButtonIcons();
  }
}

void ProgramWidget::labelsEdited() {
  if (!m_loadingLibrary && !m_libraryName.isEmpty()) {
    m_libraryModified = true;
  }
  updateProgramLabel(); // also hands the labels to the interval list
}

void ProgramWidget::updateSequenceInfo() {
  if (!m_model || !m_summaryLabel) {
    return;
  }
  int const count{m_model->getStandardModel()->columnCount()};
  // how long it lasts (a cycle repeats its intervals), and when it would end
  QStringList const order = m_model->getParamListOrder();
  int const         durationRow{static_cast<int>(order.indexOf(QStringLiteral("duration")))};
  if (durationRow < 0) {
    m_summaryLabel->clear();
    return;
  }
  auto duration = [this, durationRow](int interval) {
    return static_cast<qint64>(m_model->getStandardModel()->item(durationRow, interval)->data(Qt::UserRole).toDouble());
  };
  qint64 total{0};
  for (int interval{0}; interval < count; ++interval) {
    total += duration(interval);
  }
  QVariantMap const cycle = m_model->getCycleParams();
  int const         repeats{cycle.value("count").toInt()};
  bool const        endless{repeats == 65535};
  if ((repeats > 1) && !endless) {
    qint64 range{0};
    for (int interval{cycle.value("start").toInt()}; (interval <= cycle.value("end").toInt()) && (interval < count); ++interval) {
      range += duration(interval);
    }
    total += (repeats - 1) * range;
  }
  // the digital twin's estimate: intervals that wait for their target include reaching it
  bool const estimated{m_forecastCurrent && m_forecast.ok};
  if (estimated) {
    total = std::llround(m_forecast.totalS);
  }
  QString const length = estimated ? tr("≈ %1").arg(durationText(total)) : durationText(total);
  QString       text   = endless ? tr("%1 per pass · repeats until stopped").arg(length)
                                 : tr("%1 · ends %2 if started now")
                                     .arg(length, QDateTime::currentDateTime().addSecs(total).toString(QStringLiteral("HH:mm")));
  if (estimated && m_forecast.uncertain) {
    text += tr(" · may take longer");
  } else if (!m_forecastCurrent && m_forecaster->isBusy()) {
    text += tr(" · estimating…");
  }
  m_summaryLabel->setText(text);
  m_summaryLabel->setToolTip(
    estimated ? (m_forecastPlanned ? tr("Estimated with the chamber's digital twin from the current chamber temperature (%1 °C) "
                                        "and the AI Advisor's recommended settings for each interval. Intervals that wait for "
                                        "their target include the time to reach it.")
                                   : tr("Estimated with the chamber's digital twin from the current chamber temperature (%1 °C) "
                                        "and the current settings. Intervals that wait for their target include the time to "
                                        "reach it."))
                  .arg(QString::number(m_forecast.startTemp, 'f', 1))
    : (m_forecastCurrent && !m_forecast.error.isEmpty()) ? tr("The programmed durations (not estimated: %1)").arg(m_forecast.error)
                                                         : tr("The programmed durations"));
  updateConfirmation(); // its duration too
}

void ProgramWidget::programEdited() {
  m_forecastCurrent = false;
  m_forecastTimer.start();
  updateForecastNotes();
}

void ProgramWidget::runForecast() {
  if (!m_model) {
    return;
  }
  // while a job runs "if started now" would not start from here: the estimate stays as it was
  if (HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus()) {
    return;
  }
  ai::ForecastInput const input = forecastInputFromProgram(m_resHolder, m_model);
  m_forecastTemp                = input.startTemp;
  m_forecastPlanned             = forecastWithPlan(input);
  if (!input.error.isEmpty()) {
    showForecast(ai::forecastProgram(nullptr, input)); // why not
    return;
  }
  m_forecaster->request(input, m_forecastPlanned);
  updateSequenceInfo(); // "estimating…"
}

// A hold lasts as long as reaching its target takes, which depends on the settings: when the AI Advisor will set them,
// its estimate is the one shown (and its plan is the one started). Other intervals last their programmed time.
bool ProgramWidget::forecastWithPlan(ai::ForecastInput const &input) const {
  return m_resHolder->aiAdvisorEnabled && input.hasHold();
}

void ProgramWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  int const spacing{layout() ? layout()->spacing() : 0};
  m_leftTabs->setFixedWidth(qMax(0, (event->size().width() - spacing) / 3));
}

void ProgramWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (m_model && (m_forecastPlanned != forecastWithPlan(forecastInputFromProgram(m_resHolder, m_model)))) {
    programEdited();
  }
}

void ProgramWidget::temperatureMoved() {
  if (m_forecastTimer.isActive() || m_forecaster->isBusy()
      || (HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus())) {
    return;
  }
  bool const   online{m_resHolder->status && (StatusManager::AppState::ONLINE == m_resHolder->status->getAppState())};
  SystemValue *value = m_resHolder->states->getSysValue("temp");
  double const temp  = (online && value) ? value->getValue() : qQNaN();
  if ((std::isfinite(temp) != std::isfinite(m_forecastTemp))
      || (std::isfinite(temp) && (std::abs(temp - m_forecastTemp) > FORECAST_TEMP_STEP_C))) {
    m_forecastTimer.start();
  }
}

void ProgramWidget::showForecast(ai::ProgramForecast const &forecast) {
  if (m_forecastTimer.isActive()) {
    return; // the program changed meanwhile: another estimate follows
  }
  m_forecast        = forecast;
  m_forecastCurrent = true;
  updateSequenceInfo();
  updateForecastNotes();
}

// on the cards of the intervals that wait for their target: how long reaching it takes
void ProgramWidget::updateForecastNotes() {
  if (!m_intervalList || !m_model) {
    return;
  }
  int const   count = m_model->getStandardModel()->columnCount();
  QStringList notes;
  if (m_forecastCurrent && m_forecast.ok && (m_forecast.intervals.size() == count)) {
    ai::ForecastInput const input = forecastInputFromProgram(m_resHolder, m_model);
    for (int interval{0}; interval < count; ++interval) {
      ai::ForecastInterval const item = input.intervals.value(interval);
      ai::IntervalForecast const f    = m_forecast.intervals[interval];
      QString                    note;
      if (item.controlled && item.await) {
        note = !f.simulated ? QString{}
               : f.reached  ? tr("reaches the target in ≈ %1").arg(durationText(std::llround(f.reachS)))
                            : tr("may not reach the target within 1 h");
      }
      notes << note;
    }
  }
  m_intervalList->setNotes(notes);
}

void ProgramWidget::updateConfirmation() {
  if (!m_cycleValue || !m_model) {
    return;
  }
  QVariantMap const cycle = m_model->getCycleParams();
  int const         repeats{cycle.value("count").toInt()};
  QString const     cycleText = repeats <= 0 ? tr("None")
                                             : tr("Intervals %1 – %2 × %3")
                                                 .arg(cycle.value("start").toInt() + 1)
                                                 .arg(cycle.value("end").toInt() + 1)
                                                 .arg(repeats == 65535 ? tr("infinite") : QString::number(repeats));
  m_cycleValue->setText(cycleText);
  core::RunMetadata const metadata = m_resHolder->recorder ? m_resHolder->recorder->nextRunMetadata() : core::RunMetadata{};
  auto orDash = [](QString const &text) { return text.isEmpty() ? QStringLiteral("—") : text; };
  m_purposeValue->setText(orDash(metadata.purpose));
  // the DUT, then its mass and density when given
  QStringList dut{metadata.dut};
  for (QString const &fact : {metadata.dutMassText(), metadata.dutDensityText()}) {
    if (!fact.isEmpty()) {
      dut << fact;
    }
  }
  m_dutValue->setText(metadata.dut.isEmpty() ? tr("Not set") : dut.join(QStringLiteral(" · ")));
  m_dutValue->setStyleSheet(metadata.dut.isEmpty() ? QStringLiteral("color: %1;").arg(theme::colors().warning.name()) : QString{});
  m_operatorValue->setText(orDash(metadata.operatorName));
  int const     count{m_model->getStandardModel()->columnCount()};
  QString const name = m_libraryName.isEmpty() ? tr("Untitled program")
                       : m_libraryModified     ? tr("%1 (modified)").arg(m_libraryName)
                                               : m_libraryName;
  m_programValue->setText(tr("%1 · %2 interval(s)").arg(name).arg(count));
  m_durationValue->setText(orDash(m_summaryLabel ? m_summaryLabel->text() : QString{}));

  // the closed steps: what was chosen in them
  m_stepper->setSummary(STEP_DETAILS, metadata.purpose.isEmpty() ? tr("Name or purpose not given")
                                      : metadata.dut.isEmpty()   ? metadata.purpose
                                                                 : QStringLiteral("%1 · %2").arg(metadata.purpose, metadata.dut));
  m_stepper->setSummary(STEP_INTERVALS, tr("%1 interval(s) · cycle: %2").arg(count).arg(cycleText));
}

void ProgramWidget::updateButtonIcons() {
  if (!m_newBtn) {
    return;
  }
  theme::Colors const &c = theme::colors();
  int const            size{static_cast<int>(14 * utils::ssf())};
  m_newBtn->setIcon(theme::menuIcon("plus", Qt::white, Qt::white, size));
  m_libraryBtn->setIcon(theme::menuIcon("load", c.text, c.text, size));
  m_saveLibraryBtn->setIcon(theme::menuIcon("save", c.text, c.text, size));
}

void ProgramWidget::updateProgramLabel() {
  theme::Colors const &c = theme::colors();
  static_cast<ElidedLabel *>(m_programLabel)->setFullText(m_libraryName.isEmpty() ? tr("Untitled program") : m_libraryName);
  QColor const badgeColor = m_libraryModified ? c.warning : c.ready;
  m_libraryBadge->setText(m_libraryModified ? tr("LIBRARY · MODIFIED") : tr("LIBRARY"));
  m_libraryBadge->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %1; border-radius: %3px; padding: %4px %5px; font-weight: bold;")
                                  .arg(badgeColor.name())
                                  .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                                  .arg(static_cast<int>(3 * utils::ssf()))
                                  .arg(static_cast<int>(1 * utils::ssf()))
                                  .arg(static_cast<int>(6 * utils::ssf())));
  QFont small = font();
  small.setPointSizeF(font().pointSizeF() * 0.8);
  m_libraryBadge->setFont(small);
  m_libraryBadge->setVisible(!m_libraryName.isEmpty());
  if (m_intervalList) {
    m_intervalList->setLabels(m_labels);
  }
  updateConfirmation(); // the program's name in step 3
}

void ProgramWidget::updateRunStatus() {
  SystemValueList const *states = m_resHolder->states;
  bool const working{HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus()};
  if (!working || !states->contains("interval") || !states->contains("intervals_count")) {
    m_runStatus->clear();
    return;
  }
  // a quick setpoint (or another setpoint job) runs, not this page's program
  if (states->contains("work_type")
      && (static_cast<int>(states->getSysValue("work_type")->getValue()) == static_cast<int>(ProgramModel::ProgType::SETPOINT))) {
    m_runStatus->setText(tr("A quick setpoint is running."));
    return;
  }
  int const   current{static_cast<int>(states->getSysValue("interval")->getValue())};
  int const   count{static_cast<int>(states->getSysValue("intervals_count")->getValue())};
  QStringList lines;
  if (!m_runningName.isEmpty()) {
    lines << QStringLiteral("<b>%1</b>").arg(m_runningName.toHtmlEscaped());
  }
  lines << tr("interval %1 of %2").arg(current).arg(count);
  QString const label = m_runningLabels.value(current - 1);
  if (!label.isEmpty()) {
    lines << QStringLiteral("<i>%1</i>").arg(label.toHtmlEscaped());
  }
  if (states->contains("cycle_total") && (states->getSysValue("cycle_total")->getValue() > 1)) {
    lines << tr("cycle %1 of %2")
               .arg(static_cast<int>(states->getSysValue("cycle_cur")->getValue()))
               .arg(static_cast<int>(states->getSysValue("cycle_total")->getValue()));
  }
  m_runStatus->setText(lines.join(QStringLiteral(" · "))); // one line in the bar below the profile
}
}
