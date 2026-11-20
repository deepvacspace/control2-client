#include "programwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "programstepper.h"
#include "confdialog.h"
#include "constants.h"
#include "delayinfodialog.h"
#include "delayselectdialog.h"
#include "dialogs/messagedialog.h"
#include "experimentrecorder.h"
#include "rundetailsdialog.h"
#include "intervalcardlist.h"
#include "intervaleditdialog.h"
#include "lineeditnum.h"
#include "manualgraph.h"
#include "pressureunits.h"
#include "touchsupport.h"
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

// the chamber's pressure now in its unit (none: not connected, or no value)
std::optional<double> chamberPressure(ResHolder *resHolder) {
  bool const   online{resHolder->status && (StatusManager::AppState::ONLINE == resHolder->status->getAppState())};
  SystemValue *value = resHolder->states->getSysValue("pres");
  if (!online || !value || !std::isfinite(value->getValue())) {
    return std::nullopt;
  }
  return value->getValue();
}

// where the control charts start while the chamber's values are not known (not connected): room temperature and
// the atmosphere. Drawn only: what is sent to the chamber starts from its real values
constexpr double REFERENCE_TEMP_C{24.0};
constexpr double REFERENCE_TORR{750.0};

double drawnTemperature(ResHolder *resHolder) {
  return chamberTemperature(resHolder).value_or(REFERENCE_TEMP_C);
}

// in the chamber's pressure unit
double drawnPressure(ResHolder *resHolder) {
  if (std::optional<double> const now = chamberPressure(resHolder)) {
    return *now;
  }
  SystemValue *value = resHolder->states->getSysValue("pres");
  double const torrPerUnit{value ? pressure::toTorr(1.0, value->getLabel()) : 1.0};
  return (std::isfinite(torrPerUnit) && (torrPerUnit > 0.0)) ? REFERENCE_TORR / torrPerUnit : REFERENCE_TORR;
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

// the tabs: as wide as their names in bold, so that opening one does not change its size. The open tab is sized in
// bold by the theme (QTabBar#paneTabs::tab:selected); the others are widened here by as much. Qt does not lay the bar
// out again when the open tab changes: done here, or the tabs no longer fit and are cut behind scroll arrows.
class BoldTabBar : public QTabBar {
public:
  explicit BoldTabBar(QWidget *parent = nullptr) : QTabBar{parent} {
    connect(this, &QTabBar::currentChanged, this, [this] {
      updateGeometry();
      adjustSize();
      update();
    });
  }

protected:
  [[nodiscard]] QSize tabSizeHint(int index) const override {
    QSize size = QTabBar::tabSizeHint(index);
    if (index != currentIndex()) {
      QFont bold = font();
      bold.setBold(true);
      size.rwidth() += QFontMetrics(bold).horizontalAdvance(tabText(index)) - fontMetrics().horizontalAdvance(tabText(index));
    }
    return size;
  }
};

// a number field's value; NaN when it is not one
double fieldValue(LineEditNum const *edit) {
  bool         ok{false};
  double const value{edit->number(&ok)};
  return ok ? value : qQNaN();
}
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

  connect(m_startBtn, &QPushButton::clicked, this, &ProgramWidget::startNow);

  connect(m_startDelayedBtn, &QPushButton::clicked, this, [this] {
    if (m_model && readyToStart()) {
      auto *delaySelectDialog = new DelaySelectDialog(this);
      connect(delaySelectDialog, &DelaySelectDialog::startTimeSelected, this, [this](QDateTime startTime) {
        auto *delayInfoDialog = new DelayInfoDialog(tr("Program operation"), startTime, this);
        connect(delayInfoDialog, &QDialog::accepted, this, [this] { sendProgram(); });
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
    m_intervalList->setStartTemperature(chamberTemperature(m_resHolder));
    m_programGraph->setStartTemperature(drawnTemperature(m_resHolder)); // not connected: from the reference
    updateManualPreview(); // Manual: from where the chamber is now
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
  root->setSpacing(0); // the steps and the Manual card keep the gap to the profile

  // ---- left, a third of the width (resizeEvent): the tabs, Manual and Program (the Analysis page's tab strip: plain
  // names, the open one a white box), over the open tab's card(s)
  m_leftColumn  = new QWidget(this);
  auto *leftLay = new QVBoxLayout(m_leftColumn);
  leftLay->setContentsMargins(0, 0, 0, 0);
  leftLay->setSpacing(theme::gap());
  auto *tabsFrame = new QFrame(m_leftColumn);
  tabsFrame->setObjectName("paneHeader");
  auto *tabsLay = new QHBoxLayout(tabsFrame);
  tabsLay->setContentsMargins(theme::dp(5), theme::dp(5), theme::dp(5), theme::dp(5));
  tabsLay->setSpacing(theme::dp(6));
  m_modeTabs = new BoldTabBar(tabsFrame);
  m_modeTabs->setObjectName("paneTabs");
  m_modeTabs->setDocumentMode(true);
  m_modeTabs->setDrawBase(false);
  m_modeTabs->setExpanding(false);
  m_modeTabs->setUsesScrollButtons(false); // two tabs: always room for them
  m_modeTabs->setElideMode(Qt::ElideNone);
  m_modeTabs->setFocusPolicy(Qt::NoFocus);
  m_modeTabs->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  {
    QFont font = m_modeTabs->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    m_modeTabs->setFont(font);
  }
  m_modeTabs->addTab(tr("Manual"));
  m_modeTabs->addTab(tr("Program"));
  tabsLay->addWidget(m_modeTabs);
  tabsLay->addStretch();
  auto *tabsRow = new QHBoxLayout();
  tabsRow->setContentsMargins(0, 0, theme::gap(), 0); // as wide as the cards under it
  tabsRow->addWidget(tabsFrame);
  leftLay->addLayout(tabsRow);
  m_modeStack = new QStackedWidget(m_leftColumn);
  leftLay->addWidget(m_modeStack, 1);
  root->addWidget(m_leftColumn);
  // ---- right: the open tab's profile, the whole height
  m_graphStack = new QStackedWidget(this);
  root->addWidget(m_graphStack, 1);

  QWidget *manualCard{nullptr};
  QWidget *manualProfile{nullptr};
  buildManualPage(manualCard, manualProfile);
  m_modeStack->addWidget(manualCard);
  m_graphStack->addWidget(manualProfile);
  // ---- Program: the program in steps
  m_stepper = new ProgramStepper(m_modeStack);
  m_modeStack->addWidget(m_stepper);
  connect(m_modeTabs, &QTabBar::currentChanged, this, [this](int index) {
    m_modeStack->setCurrentIndex(index);
    m_graphStack->setCurrentIndex(index);
  });

  auto makeCard = [this, ssf](QBoxLayout *lay) {
    auto *card = new QFrame(this);
    card->setObjectName("pageCard");
    card->setLayout(lay);
    lay->setContentsMargins(theme::dp(28), theme::dp(20), theme::dp(28), theme::dp(20));
    lay->setSpacing(theme::dp(16));
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
  titleRow->setSpacing(theme::dp(20));
  m_programLabel = bold(new ElidedLabel(this));
  m_libraryBadge = new QLabel(this);
  m_summaryLabel = muted(QString{});
  titleRow->addWidget(m_programLabel);
  titleRow->addWidget(m_libraryBadge);
  titleRow->addSpacing(theme::dp(12));
  titleRow->addWidget(m_summaryLabel);
  titleRow->addStretch();
  graphLay->addLayout(titleRow);
  m_programGraph = new ProgramGraph(graphCard);
  graphLay->addWidget(m_programGraph, 1);
  m_graphStack->addWidget(graphCard);

  // ---- step 1: the experiment details, recorded with it
  auto *detailsPage = new QWidget(this);
  auto *detailsLay  = new QVBoxLayout(detailsPage);
  detailsLay->setContentsMargins(0, 0, 0, 0);
  detailsLay->setSpacing(theme::dp(16));
  m_details = new RunDetailsPanel(m_resHolder, detailsPage);
  m_details->setRequiredFields(); // the operator is the logged-in user unless typed otherwise
  detailsLay->addWidget(m_details);
  m_stepper->addStep(tr("Experiment details"), detailsPage);

  // ---- step 2: the program library, the intervals and the cycle
  auto *intervalsPage = new QWidget(this);
  auto *intervalsLay  = new QVBoxLayout(intervalsPage);
  intervalsLay->setContentsMargins(0, 0, 0, 0);
  intervalsLay->setSpacing(theme::dp(16));
  {
    auto *libraryRow = new QHBoxLayout();
    libraryRow->setSpacing(theme::dp(16));
    auto *loadBtn = new QPushButton(tr("Load from library"), intervalsPage);
    loadBtn->setToolTip(tr("Load a program from the program library (also those shared by paired computers)"));
    auto *saveBtn = new QPushButton(tr("Save to library"), intervalsPage);
    saveBtn->setToolTip(tr("Save this program in the program library, by name"));
    for (QPushButton *button : {loadBtn, saveBtn}) {
      button->setFixedHeight(theme::dp(56));
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
    cycleLay->setContentsMargins(theme::dp(24), theme::dp(16), theme::dp(24), theme::dp(20));
    cycleLay->setSpacing(theme::dp(8));
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
  m_newBtn->setFixedHeight(theme::dp(52));
  m_newBtn->setFocusPolicy(Qt::NoFocus);
  m_stepper->setHeaderWidget(STEP_INTERVALS, m_newBtn);

  // ---- step 3: the experiment as it will start; Start, Delay start, Stop
  auto *startPage = new QWidget(this);
  auto *startLay  = new QVBoxLayout(startPage);
  startLay->setContentsMargins(0, 0, 0, 0);
  startLay->setSpacing(theme::dp(20));
  auto *facts = new QGridLayout();
  facts->setHorizontalSpacing(theme::dp(24));
  facts->setVerticalSpacing(theme::dp(12));
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
  buttonsLay->setSpacing(theme::dp(16));
  m_startBtn = new QPushButton(tr("Start"), startPage);
  m_startBtn->setObjectName("startButton");
  m_startDelayedBtn = new QPushButton(tr("Delay\nstart").replace('\n', ' '), startPage);
  m_stopBtn         = new StopButton(startPage);
  for (QPushButton *btn : {m_startBtn, m_startDelayedBtn, static_cast<QPushButton *>(m_stopBtn)}) {
    btn->setFixedHeight(theme::dp(67));
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

  // not for an Observer; idle or finished (while a job runs, a tap replaces a setpoint or says to stop a program
  // first: startNow()); a delayed start only from idle
  bool const enableStart{((HardwareStatus::HardwareStatusEnum::IDLE == hardWorkStatus)
                          || (HardwareStatus::HardwareStatusEnum::FINISH == hardWorkStatus) || working)
                         && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel) && (intervalCount > 0)};
  m_startBtn->setEnabled(enableStart);
  m_startDelayedBtn->setEnabled(enableStart && !working);
  m_stopBtn->setEnabled(working && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel));

  m_cycleWidget->setIntervalsCount(intervalCount);

  bool const editable{m_accessLevel >= AccessManager::AccessLevel::USER_EDIT_2};
  m_saveLibraryBtn->setEnabled(editable && (intervalCount > 0));
  m_newBtn->setEnabled(editable);
  m_intervalList->setEditable(editable);
  m_cycleWidget->setEnabled(editable && (intervalCount > 0)); // one interval too: repeated, or held until stopped
  updateManualButtons();
}

bool ProgramWidget::setpointRunning() const {
  SystemValueList const *states = m_resHolder->states;
  return (HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus()) && states->contains("work_type")
         && (static_cast<int>(states->getSysValue("work_type")->getValue()) == static_cast<int>(ProgramModel::ProgType::SETPOINT));
}

bool ProgramWidget::readyToStart() {
  if ((HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus()) && !setpointRunning()) {
    (new MessageDialog(tr("Program control"), tr("A program is running. Stop it before starting another."), this))->open();
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

void ProgramWidget::startNow() {
  if (!m_model || !readyToStart()) {
    return;
  }
  if (!setpointRunning()) {
    sendProgram();
    return;
  }
  // the chamber goes on to the new setpoint without stopping: only once the user agrees
  auto *confirm = new ConfDialog(tr("Replace the setpoint"),
                                 tr("A setpoint is running. Replace it with this experiment?\n\nThe chamber goes on to the new "
                                    "setpoint without stopping; the running one is saved as stopped."),
                                 tr("Replace"), tr("Cancel"), this);
  confirm->setIconKind(QStringLiteral("warning"));
  connect(confirm, &QDialog::accepted, this, [this] { sendProgram(true); });
  confirm->open();
}

void ProgramWidget::sendProgram(bool replace) {
  if (!readyToStart()) { // also when a delayed start comes: a job may have been started meanwhile
    return;
  }
  bool const replacing{setpointRunning()};
  if (replacing && !replace) { // a delayed start: a setpoint started meanwhile is not replaced unasked
    (new MessageDialog(tr("Program control"), tr("A setpoint is running. The delayed start was not done."), this))->open();
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
  // one interval goes as a setpoint job: the next Start replaces it without stopping
  int const count{m_model->getStandardModel()->columnCount()};
  m_model->setProgType((count == 1) ? ProgramModel::ProgType::SETPOINT : ProgramModel::ProgType::PROG);
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, m_model->makeSendData());
  if (replacing && m_resHolder->recorder) {
    m_resHolder->recorder->jobReplaced();
  }
  QString const actionMsg{
    QString(tr("Starting program operation. %1 interval(s).")).arg(count)};
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
    if (duration(interval) >= HOLD_UNTIL_STOPPED) { // the intervals before it, then held
      m_summaryLabel->setText((total > 0) ? tr("%1, then held until stopped").arg(durationText(total)) : tr("Held until stopped"));
      m_summaryLabel->setToolTip(tr("The programmed durations"));
      updateConfirmation(); // its duration too
      return;
    }
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
  // the tabs with the steps (Program) or the card (Manual) a third of the width, the profile the rest
  m_leftColumn->setFixedWidth(qMax(0, event->size().width() / 3));
}

void ProgramWidget::showManual() {
  m_modeTabs->setCurrentIndex(0);
}

void ProgramWidget::showProgram() {
  m_modeTabs->setCurrentIndex(1);
}

void ProgramWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  m_details->fillDefaultName(); // "Untitled 2026-11-06 10:30" when no name was given: when the page is opened
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
  // while a program runs: step 3 only (its Stop); a setpoint running leaves every step open (Start replaces it). Step 3
  // opens when a job starts; back to the other steps once the chamber is stopped
  bool const replaceable{setpointRunning()};
  updateManualButtons(); // what runs (the recorder knows its kind once it has begun it)
  m_stepper->setLocked(working && !replaceable);
  if (working && (!replaceable || !m_working)) {
    m_stepper->setCurrent(STEP_START);
  }
  m_working = working;
  if (!working || !states->contains("interval") || !states->contains("intervals_count")) {
    m_runStatus->clear();
    return;
  }
  // a setpoint job not started from this page (the PLC's panel, an advisor)
  if (replaceable && m_resHolder->recorder && (m_resHolder->recorder->runningJobKind() != QLatin1String("program"))) {
    m_runStatus->setText(tr("A setpoint is running."));
    return;
  }
  // a whole number of the PLC; -1 while it has not been received
  auto whole = [states](char const *name) {
    double const value{states->contains(name) ? static_cast<double>(states->getSysValue(name)->getValue()) : qQNaN()};
    return std::isfinite(value) ? static_cast<int>(std::lround(value)) : -1;
  };
  int const current{whole("interval")};
  int const count{whole("intervals_count")};
  if ((current < 0) || (count < 0)) {
    m_runStatus->clear();
    return;
  }
  QStringList lines;
  if (!m_runningName.isEmpty()) {
    lines << QStringLiteral("<b>%1</b>").arg(m_runningName.toHtmlEscaped());
  }
  lines << tr("interval %1 of %2").arg(current).arg(count);
  QString const label = m_runningLabels.value(current - 1);
  if (!label.isEmpty()) {
    lines << QStringLiteral("<i>%1</i>").arg(label.toHtmlEscaped());
  }
  if ((whole("cycle_total") > 1) && (whole("cycle_cur") >= 0)) {
    lines << tr("cycle %1 of %2").arg(whole("cycle_cur")).arg(whole("cycle_total"));
  }
  m_runStatus->setText(lines.join(QStringLiteral(" · "))); // one line in the bar below the profile
}

// ---- the Manual tab

void ProgramWidget::buildManualPage(QWidget *&cardColumn, QWidget *&profile) {
  // left, under the tabs: one card, scrolled when the page is short
  m_manualColumn = new QWidget(this);
  auto *columnLay = new QVBoxLayout(m_manualColumn);
  columnLay->setContentsMargins(0, 0, theme::gap(), 0); // the gap to the profile, as the steps keep it
  auto *area = new QScrollArea(m_manualColumn);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  area->viewport()->setAutoFillBackground(false);
  area->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  touch::enableScrolling(area);
  columnLay->addWidget(area);
  auto *holder = new QWidget(area);
  holder->setAutoFillBackground(false);
  auto *holderLay = new QVBoxLayout(holder);
  holderLay->setContentsMargins(0, 0, 0, 0);
  area->setWidget(holder);

  auto *card = new QFrame(holder);
  card->setObjectName("pageCard");
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(theme::dp(28), theme::dp(24), theme::dp(28), theme::dp(24));
  cardLay->setSpacing(theme::dp(20));
  holderLay->addWidget(card);
  holderLay->addStretch();

  // the DUT, optional (the name is "Manual" and the start's date and time, the operator the logged-in user)
  m_manualDut = new RunDetailsForm(card);
  m_manualDut->setDutOnly();
  cardLay->addWidget(m_manualDut);
  auto *line = new QFrame(card);
  line->setObjectName("cardDivider");
  line->setFixedHeight(theme::dp(1));
  cardLay->addWidget(line);

  // the setpoints held until stopped: temperature over pressure, each on or off (at least one on). The temperature at
  // full speed, or at the ramp rate given. No ramp or hold time: held until stopped, the ramp's time follows from the
  // rate and the chamber's temperature at the start
  QVariantList const params = m_resHolder->settings->controlParams();
  auto               param  = [&params](QString const &name) {
    for (QVariant const &item : params) {
      if (item.toMap().value("name").toString() == name) {
        return item.toMap();
      }
    }
    return QVariantMap{};
  };
  m_manualPresUnit = QStringLiteral("Torr"); // a programmed pressure is in the chamber's unit
  if (m_resHolder->states->contains(QStringLiteral("pres"))) {
    QString const unit = pressure::unitOf(m_resHolder->states->getSysValue(QStringLiteral("pres"))->getLabel());
    if (!unit.isEmpty()) {
      m_manualPresUnit = (unit == QLatin1String("mmhg")) ? QStringLiteral("mmHg") : (unit == QLatin1String("torr")) ? QStringLiteral("Torr") : unit;
    }
  }
  auto *grid = new QGridLayout();
  grid->setHorizontalSpacing(theme::gap());
  grid->setVerticalSpacing(theme::dp(8));
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 1);
  int  row{0};
  auto title = [card](QString const &text) {
    auto *label = new QLabel(text, card);
    label->setProperty("muted", true);
    return label;
  };
  auto fieldSize = [](QWidget *field) {
    field->setMinimumHeight(theme::dp(56));
    field->setMinimumWidth(theme::dp(80)); // not its text's width: the card fits the column
    field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  };
  for (auto const &[name, text, unit, box, edit, rate] :
       {std::tuple{QStringLiteral("temp"), tr("Temperature"), QStringLiteral("°C"), &m_manualTempOn, &m_manualTemp, &m_manualRate},
        std::tuple{QStringLiteral("pres"), tr("Pressure"), m_manualPresUnit, &m_manualPresOn, &m_manualPres, &m_manualPresRate}}) {
    QVariantMap const p = param(name);
    bool const        pressure{name == QLatin1String("pres")};
    bool const        scientific{p.value("notation").toString() == QLatin1String("scientific")};
    *box = new QCheckBox(text, card);
    (*box)->setFocusPolicy(Qt::NoFocus);
    (*box)->setProperty("boxed", true); // the Channels list's tick box
    (*box)->setMinimumHeight(theme::dp(44));
    *edit = new LineEditNum(p.value("min").toDouble(), p.value("max").toDouble(), p.value("prec", 1).toInt(), true, card);
    (*edit)->setIsScientific(scientific);
    (*edit)->setValue(p.value("value").toFloat());
    fieldSize(*edit);
    // a ramp rate given: a ramp at it from the chamber's value, then the hold; none: full speed, then the hold. The
    // pressure's ramp goes as a step a minute (the chamber ramps the temperature only)
    *rate = pressure ? new LineEditNum(p.value("min").toDouble(), p.value("max").toDouble(), p.value("prec", 1).toInt(), true, card)
                     : new LineEditNum(0.01, 10.0, 2, true, card);
    (*rate)->setIsScientific(pressure && scientific);
    (*rate)->setAllowEmpty(true);
    (*rate)->clear();
    (*rate)->setToolTip(pressure ? tr("Empty: to the setpoint at full speed, then held until stopped.\n"
                                      "A rate: from the chamber's pressure to the setpoint at this rate, a step a minute, then held "
                                      "until stopped.")
                                 : tr("Empty: to the setpoint at full speed, then held until stopped.\n"
                                      "A rate: a linear ramp at it from the chamber's temperature, then held until stopped."));
    fieldSize(*rate);
    if (row > 0) {
      grid->setRowMinimumHeight(row++, theme::dp(14)); // apart from the one above
    }
    grid->addWidget(*box, row++, 0, 1, 2);
    QLabel *setpointTitle = title(tr("Setpoint, %1").arg(unit));
    QLabel *rateTitle     = title(tr("Ramp rate, %1/min").arg(unit));
    grid->addWidget(setpointTitle, row, 0);
    grid->addWidget(rateTitle, row++, 1);
    grid->addWidget(*edit, row, 0);
    grid->addWidget(*rate, row++, 1);
    connect(*box, &QCheckBox::toggled, this, [this, b = *box] { manualToggled(b); });
    connect(*edit, &LineEditNum::userEditingFinished, this, &ProgramWidget::updateManualPreview);
    connect(*rate, &LineEditNum::userEditingFinished, this, &ProgramWidget::updateManualPreview);
    // a chamber without pressure control: the pressure is not offered
    bool const available{!p.isEmpty()};
    for (QWidget *part : {static_cast<QWidget *>(*box), static_cast<QWidget *>(*edit), static_cast<QWidget *>(*rate),
                          static_cast<QWidget *>(setpointTitle), static_cast<QWidget *>(rateTitle)}) {
      part->setVisible(available);
    }
  }
  m_manualTempOn->setChecked(true);
  m_manualPresOn->setChecked(m_manualTempOn->isHidden());
  updateManualFields();
  cardLay->addLayout(grid);

  m_manualStatus = new QLabel(card);
  m_manualStatus->setProperty("muted", true);
  m_manualStatus->setWordWrap(true);
  cardLay->addWidget(m_manualStatus);

  // Start, Delay start, Stop (the Program tab's)
  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(theme::dp(16));
  m_manualStartBtn = new QPushButton(tr("Start"), card);
  m_manualStartBtn->setObjectName("startButton");
  m_manualDelayBtn = new QPushButton(tr("Delay\nstart").replace('\n', ' '), card);
  m_manualStopBtn  = new StopButton(card);
  for (QPushButton *btn : {m_manualStartBtn, m_manualDelayBtn, static_cast<QPushButton *>(m_manualStopBtn)}) {
    btn->setFixedHeight(theme::dp(67));
    btn->setMinimumWidth(1); // not their text's width (0 would leave it): the card fits the column
    btn->setFocusPolicy(Qt::NoFocus);
    QFont font = btn->font();
    font.setBold(true);
    font.setPointSizeF(QWidget::font().pointSizeF() * 1.1);
    btn->setFont(font);
    buttons->addWidget(btn, 1);
  }
  cardLay->addLayout(buttons);
  cardColumn = m_manualColumn;

  // right: the setpoints as a profile, held until stopped, from where the chamber is now (when connected)
  auto *graphCard = new QFrame(this);
  graphCard->setObjectName("pageCard");
  auto *graphLay = new QVBoxLayout(graphCard);
  graphLay->setContentsMargins(theme::dp(28), theme::dp(20), theme::dp(28), theme::dp(20));
  graphLay->setSpacing(theme::dp(16));
  auto *titleRow = new QHBoxLayout();
  titleRow->setSpacing(theme::dp(20));
  auto *graphTitle = new QLabel(tr("Manual"), graphCard);
  QFont bold       = graphTitle->font();
  bold.setBold(true);
  graphTitle->setFont(bold);
  m_manualSummary = new QLabel(graphCard);
  m_manualSummary->setProperty("muted", true);
  titleRow->addWidget(graphTitle);
  titleRow->addSpacing(theme::dp(12));
  titleRow->addWidget(m_manualSummary);
  titleRow->addStretch();
  graphLay->addLayout(titleRow);
  m_manualModel = new ProgramModel{params, ProgramModel::ProgType::SETPOINT, this};
  m_manualGraph = new ManualGraph(graphCard);
  m_manualGraph->setPressureUnit(m_manualPresUnit);
  m_manualGraph->setColors(QColor(param(QStringLiteral("temp")).value("color", QStringLiteral("#d4122e")).toString()),
                           QColor(param(QStringLiteral("pres")).value("color", QStringLiteral("#1f5fff")).toString()));
  graphLay->addWidget(m_manualGraph, 1);
  updateManualPreview();
  profile = graphCard;

  connect(m_manualStartBtn, &QPushButton::clicked, this, &ProgramWidget::manualStartNow);
  connect(m_manualDelayBtn, &QPushButton::clicked, this, [this] {
    if (!manualReady()) {
      return;
    }
    auto *delaySelectDialog = new DelaySelectDialog(this);
    connect(delaySelectDialog, &DelaySelectDialog::startTimeSelected, this, [this](QDateTime startTime) {
      auto *delayInfoDialog = new DelayInfoDialog(tr("Manual control"), startTime, this);
      connect(delayInfoDialog, &QDialog::accepted, this, [this] { sendManual(); });
      delayInfoDialog->open();
    });
    delaySelectDialog->open();
  });
  connect(m_manualStopBtn, &StopButton::confirmPressed, this, [this]() {
    m_resHolder->tcpGate->sendTextCmd("stop");
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Chamber operation stopped by user"));
  });
}

void ProgramWidget::manualToggled(QCheckBox *box) {
  // the last one on stays on: something is held
  if (!manualOn(m_manualTempOn) && !manualOn(m_manualPresOn)) {
    QSignalBlocker const block(box);
    box->setChecked(true);
  }
  updateManualFields();
  updateManualPreview();
}

bool ProgramWidget::manualOn(QCheckBox const *box) const {
  return box->isChecked() && !box->isHidden();
}

bool ProgramWidget::manualRamps(LineEditNum const *rate) const {
  QCheckBox const *box = (rate == m_manualRate) ? m_manualTempOn : m_manualPresOn;
  return manualOn(box) && !rate->text().trimmed().isEmpty();
}

void ProgramWidget::updateManualFields() {
  bool const temp{m_manualTempOn->isChecked()};
  bool const pres{m_manualPresOn->isChecked()};
  m_manualTemp->setEnabled(temp);
  m_manualRate->setEnabled(temp);
  m_manualPres->setEnabled(pres);
  m_manualPresRate->setEnabled(pres);
}

void ProgramWidget::updateManualPreview() {
  if (!m_manualGraph) {
    return;
  }
  // each variable switched on: from the chamber's value to its setpoint (at its rate, or full speed), then held
  auto line = [this](QCheckBox const *box, LineEditNum const *edit, LineEditNum const *rate, std::optional<double> now) {
    ManualGraph::Line result;
    result.on     = manualOn(box) && std::isfinite(fieldValue(edit));
    result.now    = now;
    result.target = fieldValue(edit);
    if (manualRamps(rate) && (fieldValue(rate) > 0.0)) {
      result.rate = fieldValue(rate);
    }
    return result;
  };
  // not connected: from the reference values
  m_manualGraph->setLines(line(m_manualTempOn, m_manualTemp, m_manualRate, drawnTemperature(m_resHolder)),
                          line(m_manualPresOn, m_manualPres, m_manualPresRate, drawnPressure(m_resHolder)));
  QStringList ramps;
  if (manualRamps(m_manualRate) && (fieldValue(m_manualRate) > 0.0)) {
    ramps << tr("%1 °C/min").arg(QString::number(fieldValue(m_manualRate), 'f', 2));
  }
  if (manualRamps(m_manualPresRate) && (fieldValue(m_manualPresRate) > 0.0)) {
    ramps << tr("%1 %2/min").arg(utils::formatSci10(fieldValue(m_manualPresRate)), m_manualPresUnit);
  }
  QString summary = ramps.isEmpty() ? tr("Full speed, then held until stopped")
                                    : tr("Ramp at %1, then held until stopped").arg(ramps.join(tr(" and ")));
  m_manualSummary->setText(summary);
  // not connected: where the lines start from, as a tip (the summary stays short)
  m_manualSummary->setToolTip(chamberTemperature(m_resHolder)
                                ? QString{}
                                : tr("No connection: drawn from %1 °C and %2 Torr.").arg(REFERENCE_TEMP_C, 0, 'f', 0).arg(REFERENCE_TORR, 0, 'f', 0));
}

bool ProgramWidget::buildManualProgram() {
  // the setpoints switched on, held until stopped (the last interval). Before it, when something ramps: the ramp from
  // the chamber's values now. The temperature's in one linear ramp interval; with a pressure ramp, in steps of a minute
  // (at most MAX_STEPS) for both: the pressure's setpoint stepped along its line, the temperature's ramped along its own
  constexpr int      MAX_STEPS{100};
  QStringList const  order  = m_manualModel->getParamListOrder();
  QVariantList const params = m_manualModel->getParamListVariant();
  QVariantMap        base;
  for (int i{0}; i < order.size(); ++i) {
    base[order[i]] = params.value(i).toMap().value(QStringLiteral("value"));
  }
  int const    tempRow{static_cast<int>(order.indexOf(QStringLiteral("temp")))};
  int const    presRow{static_cast<int>(order.indexOf(QStringLiteral("pres")))};
  bool const   tempOn{manualOn(m_manualTempOn) && (tempRow >= 0)};
  bool const   presOn{manualOn(m_manualPresOn) && (presRow >= 0)};
  double const tempTarget{fieldValue(m_manualTemp)};
  double const presTarget{fieldValue(m_manualPres)};
  std::optional<double> const tempNow = chamberTemperature(m_resHolder);
  std::optional<double> const presNow = chamberPressure(m_resHolder);
  auto rampSeconds = [](bool ramps, std::optional<double> now, double target, double rate) {
    if (!ramps || !now || !(rate > 0.0)) {
      return 0;
    }
    return static_cast<int>(qBound(0.0, std::round(std::abs(target - *now) / rate * 60.0), static_cast<double>(HOLD_UNTIL_STOPPED - 1)));
  };
  int const tempSeconds{rampSeconds(tempOn && manualRamps(m_manualRate), tempNow, tempTarget, fieldValue(m_manualRate))};
  int const presSeconds{rampSeconds(presOn && manualRamps(m_manualPresRate), presNow, presTarget, fieldValue(m_manualPresRate))};
  int const total{std::max(tempSeconds, presSeconds)};
  // a pressure ramp: a step a minute (longer ones when there would be too many); the intervals also end where each
  // ramp does, so the temperature's last part keeps its rate
  QVector<int> ends;
  if (presSeconds > 0) {
    int const step{std::max(60, (total + MAX_STEPS - 3) / (MAX_STEPS - 2))};
    for (int at{step}; at < total; at += step) {
      ends << at;
    }
  }
  for (int const at : {tempSeconds, presSeconds, total}) {
    if ((at > 0) && !ends.contains(at)) {
      ends << at;
    }
  }
  std::sort(ends.begin(), ends.end());

  auto interval = [&](double tempValue, double presValue, quint32 flags, int duration) {
    QVariantMap values = base;
    if (tempOn) {
      utils::setBitOfDword(flags, tempRow, true);
      values[QStringLiteral("temp")] = tempValue;
    }
    if (presOn) {
      utils::setBitOfDword(flags, presRow, true);
      values[QStringLiteral("pres")] = presValue;
    }
    values[QStringLiteral("duration")] = duration;
    values[QStringLiteral("flags")]    = flags;
    return values;
  };
  QVariantList intervals;
  int from{0};
  for (int const to : std::as_const(ends)) {
    bool const   tempRamping{(tempSeconds > 0) && (from < tempSeconds)};
    double const tempValue{tempRamping ? *tempNow + (tempTarget - *tempNow) * std::min(to, tempSeconds) / tempSeconds : tempTarget};
    double const presValue{(presSeconds > 0) ? *presNow + (presTarget - *presNow) * std::min(to, presSeconds) / presSeconds : presTarget};
    quint32      flags{0U};
    utils::setBitOfDword(flags, RAMP_BIT, tempRamping); // linear from the previous target (the first: the chamber's)
    intervals << interval(tempValue, presValue, flags, to - from);
    from = to;
  }
  quint32 hold{0U};
  utils::setBitOfDword(hold, AWAIT_BIT, true);
  intervals << interval(tempTarget, presTarget, hold, HOLD_UNTIL_STOPPED);
  m_manualModel->setProgramVariant({{QStringLiteral("intervals"), intervals},
                                    {QStringLiteral("cycle"), QVariantMap{{"start", 0}, {"end", 0}, {"count", 0}}}});
  return true;
}

bool ProgramWidget::manualReady() {
  if ((HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus()) && !setpointRunning()) {
    showManual();
    (new MessageDialog(tr("Manual control"), tr("A program is running. Stop it before starting manual control."), this))->open();
    return false;
  }
  if (!manualOn(m_manualTempOn) && !manualOn(m_manualPresOn)) {
    (new MessageDialog(tr("Manual control"), tr("Switch on the temperature or the pressure to hold."), this))->open();
    return false;
  }
  for (auto const &[box, edit, what] : {std::tuple{m_manualTempOn, m_manualTemp, tr("temperature")},
                                        std::tuple{m_manualPresOn, m_manualPres, tr("pressure")}}) {
    if (box->isChecked() && !box->isHidden() && !std::isfinite(fieldValue(edit))) {
      (new MessageDialog(tr("Manual control"), tr("Give the %1 a value.").arg(what), this))->open();
      return false;
    }
  }
  for (auto const &[rate, known, text] :
       {std::tuple{m_manualRate, chamberTemperature(m_resHolder).has_value(), tr("The chamber's temperature is not known: a ramp at a rate starts from it.")},
        std::tuple{m_manualPresRate, chamberPressure(m_resHolder).has_value(), tr("The chamber's pressure is not known: a ramp at a rate starts from it.")}}) {
    if (!manualRamps(rate)) {
      continue;
    }
    if (!(fieldValue(rate) > 0.0)) {
      (new MessageDialog(tr("Manual control"), tr("The ramp rate must be a number above zero (or left empty: full speed)."), this))->open();
      return false;
    }
    if (!known) {
      (new MessageDialog(tr("Manual control"), text, this))->open();
      return false;
    }
  }
  if (!m_manualDut->valid()) {
    (new MessageDialog(tr("Manual control"), tr("The DUT's mass and density must be numbers (or left empty)."), this))->open();
    return false;
  }
  return true;
}

void ProgramWidget::manualStartNow() {
  if (!manualReady()) {
    return;
  }
  if (!setpointRunning()) {
    sendManual();
    return;
  }
  // the chamber goes on to the new setpoints without stopping: only once the user agrees
  auto *confirm = new ConfDialog(tr("Replace the setpoint"),
                                 tr("A setpoint is running. Replace it with these setpoints?\n\nThe chamber goes on to the new "
                                    "setpoints without stopping; the running one is saved as stopped."),
                                 tr("Replace"), tr("Cancel"), this);
  confirm->setIconKind(QStringLiteral("warning"));
  connect(confirm, &QDialog::accepted, this, [this] { sendManual(true); });
  confirm->open();
}

void ProgramWidget::sendManual(bool replace) {
  if (!manualReady()) { // also when a delayed start comes: a job may have been started meanwhile
    return;
  }
  bool const replacing{setpointRunning()};
  if (replacing && !replace) { // a delayed start: a setpoint started meanwhile is not replaced unasked
    (new MessageDialog(tr("Manual control"), tr("A setpoint is running. The delayed start was not done."), this))->open();
    return;
  }
  buildManualProgram(); // the ramps from the chamber's values now

  // recorded as "Manual 2026-11-14 19:36" (the start's time, as the Program tab's default name), by the logged-in user,
  // with the DUT of this card; the Program tab's details are left as they are
  QString const     name = tr("Manual %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
  core::RunMetadata metadata;
  if (m_resHolder->recorder) {
    metadata = m_manualDut->applyTo(m_resHolder->recorder->nextRunMetadata()); // tags and notes kept
  }
  metadata.purpose      = name;
  metadata.operatorName = m_resHolder->users ? m_resHolder->users->getCurrentUserName() : QString{};
  QStringList setpoints;
  if (m_manualTempOn->isChecked() && !m_manualTempOn->isHidden()) {
    QString const temp = QStringLiteral("%1 °C").arg(QString::number(fieldValue(m_manualTemp), 'f', 1));
    setpoints << (manualRamps(m_manualRate) ? tr("%1 at %2 °C/min").arg(temp, QString::number(fieldValue(m_manualRate), 'f', 2)) : temp);
  }
  if (m_manualPresOn->isChecked() && !m_manualPresOn->isHidden()) {
    QString const pres = QStringLiteral("%1 %2").arg(utils::formatSci10(fieldValue(m_manualPres)), m_manualPresUnit);
    setpoints << (manualRamps(m_manualPresRate)
                    ? tr("%1 at %2 %3/min").arg(pres, utils::formatSci10(fieldValue(m_manualPresRate)), m_manualPresUnit)
                    : pres);
  }
  if (m_resHolder->recorder) {
    ExperimentRecorder::JobInfo job{QStringLiteral("manual_setpoint"), name, {}, m_manualModel->programVariant(),
                                    QJsonObject{{"setpoints", QJsonArray::fromStringList(setpoints)}}};
    job.metadata = metadata;
    m_resHolder->recorder->setNextJob(job);
  }
  // as the Program tab: one interval goes as a setpoint job (the next Start replaces it without stopping), a ramp and
  // its hold as a program
  m_manualModel->setProgType((m_manualModel->getStandardModel()->columnCount() == 1) ? ProgramModel::ProgType::SETPOINT
                                                                                      : ProgramModel::ProgType::PROG);
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, m_manualModel->makeSendData());
  if (replacing && m_resHolder->recorder) {
    m_resHolder->recorder->jobReplaced();
  }
  m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Starting manual control: %1, held until stopped.").arg(setpoints.join(QStringLiteral(", "))));
  updateRunStatus();
}

void ProgramWidget::updateManualButtons() {
  if (!m_manualStartBtn) {
    return;
  }
  HardwareStatus::HardwareStatusEnum const status{m_resHolder->hard_status->getHardwareStatus()};
  bool const                               working{HardwareStatus::HardwareStatusEnum::WORK == status};
  bool const                               observer{AccessManager::AccessLevel::SPECTATOR_0 == m_accessLevel};
  // as the Program tab's: idle, finished, or a setpoint running (replaced); a delayed start only when nothing runs
  bool const enableStart{((HardwareStatus::HardwareStatusEnum::IDLE == status) || (HardwareStatus::HardwareStatusEnum::FINISH == status) || working)
                         && !observer};
  m_manualStartBtn->setEnabled(enableStart);
  m_manualDelayBtn->setEnabled(enableStart && !working);
  m_manualStopBtn->setEnabled(working && !observer);
  // what runs
  QString status_;
  if (working) {
    QString const kind = m_resHolder->recorder ? m_resHolder->recorder->runningJobKind() : QString{};
    bool const manual{kind == QLatin1String("manual_setpoint")};
    status_ = (manual && setpointRunning()) ? tr("Manual control is running. Start replaces its setpoints.")
              : manual                      ? tr("Manual control is running (a ramp, then the hold). Stop it to start another.")
              : setpointRunning()           ? tr("A setpoint is running. Start replaces it.")
                                            : tr("A program is running.");
  }
  m_manualStatus->setText(status_);
  m_manualStatus->setVisible(!status_.isEmpty());
}
}
