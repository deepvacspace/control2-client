#include "offlineadvisorwidget.h"

#include "theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QDialog>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

#include "accessmanager.h"
#include "actionmanager.h"
#include "experimentrecorder.h"
#include "advisorcem.h"
#include "confdialog.h"
#include "constants.h"
#include "gatetcp.h"
#include "lineeditnum.h"
#include "programmodel.h"
#include "qcustomplot.h"
#include "settingsmanager.h"
#include "settingstransformer.h"
#include "systemvalue.h"
#include "systemvaluelist.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
QString formatNumber(double value, int prec = 2) {
  return std::isfinite(value) ? QString::number(value, 'f', prec) : QStringLiteral("—");
}

QString formatPid(double kp, double ki, double kd) {
  return QStringLiteral("Kp=%1  Ki=%2  Kd=%3")
    .arg(formatNumber(kp, 1), formatNumber(ki, 1), formatNumber(kd, 1));
}

constexpr qint64 TELEMETRY_TIMEOUT_MS{10'000}; // no telemetry for this long aborts the run

// the search cannot be interrupted: Escape does not close its window
class BusyDialog : public QDialog {
public:
  using QDialog::QDialog;
  void reject() override {}
};
} // namespace

OfflineAdvisorWidget::OfflineAdvisorWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder} {
  m_pidGuard = new ai::ManualPidGuard(m_resHolder, QStringLiteral("offline_advisor"), this);
  initGui();
  loadModel();

  connect(m_pidGuard, &ai::ManualPidGuard::restoreConfirmed, this, [this] {
    m_pidLabel->setText(tr("Manual PID restored (confirmed by the chamber)"));
    updateRunButtonEnabled();
  });
  connect(m_pidGuard, &ai::ManualPidGuard::restoreFailed, this, [this](QString const &message) {
    setStatusText(message, true);
    updateRunButtonEnabled();
  });
  connect(m_pidGuard, &ai::ManualPidGuard::recoveredPreviousSession, this,
          [this](QString const &reason, bool jobMayBeRunning) {
            setStatusText(tr("Restored the manual PID the AI Advisor had replaced (%1)").arg(reason), true);
            ai::notifyRecoveredSession(this, m_pidGuard, reason, jobMayBeRunning);
          });
  if (m_resHolder->users) {
    connect(m_resHolder->users, &AccessManager::userSelected, this, &OfflineAdvisorWidget::onAccessChanged);
  }

  connect(&m_progressTimer, &QTimer::timeout, this, &OfflineAdvisorWidget::onProgressTick);
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &OfflineAdvisorWidget::onTelemetryUpdated);
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &OfflineAdvisorWidget::onAppStateChanged);
  connect(m_resHolder->tcpGate, &GateTcp::disconnected, this, &OfflineAdvisorWidget::onGateDisconnected);
  connect(m_resHolder->tcpGate, &GateTcp::alarmReceived, this, &OfflineAdvisorWidget::onAlarmReceived);

  updateRunButtonEnabled();
}

OfflineAdvisorWidget::~OfflineAdvisorWidget() {
  if (m_running) {
    m_progressTimer.stop();
    m_pidGuard->restoreNow(true, tr("application closed during an AI Advisor experiment"));
  }
  if (m_tuneWorker) {
    m_tuneWorker->wait();
  }
}

void OfflineAdvisorWidget::loadModel() {
  QDir const modelDir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));

  bool const ok = m_model.load(modelDir.filePath("twin_model.onnx"), modelDir.filePath("twin_model.json"));
  setStatusText(ok ? QString() : m_model.statusMessage(), !ok); // only problems are shown
  updateRunButtonEnabled();
}

void OfflineAdvisorWidget::updateRunButtonEnabled() {
  bool const online = m_resHolder->status && (StatusManager::AppState::ONLINE == m_resHolder->status->getAppState());
  bool const allowed = ai::advisorControlDenied(m_resHolder).isEmpty();
  bool const settled = !m_pidGuard || !m_pidGuard->isPending(); // previous restore confirmed
  // without the chamber the Advisor only calculates and predicts (no job is sent)
  m_runBtn->setText(online ? tr("Run experiment") : tr("Calculate"));
  m_runBtn->setToolTip(online ? QString() : tr("The chamber is not connected: the best coefficients are calculated and the "
                                               "experiment is predicted with the digital twin; nothing is sent to the chamber"));
  m_runBtn->setEnabled(m_model.isLoaded() && (!online || (allowed && settled)) && !m_running && !m_tuneWorker);
}

namespace {
QFrame *advisorCard(QWidget *parent, QString const &title) {
  double const ssf{utils::ssf()};
  auto        *card = new QFrame(parent);
  card->setObjectName("pageCard");
  auto *lay = new QVBoxLayout(card);
  lay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  lay->setSpacing(8 * ssf);
  if (!title.isEmpty()) {
    auto *label = new QLabel(title, card);
    label->setObjectName("cardTitle");
    lay->addWidget(label);
  }
  return card;
}
} // namespace

void OfflineAdvisorWidget::initGui() {
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 8 * ssf, 0, 0);
  mainLay->setSpacing(10 * ssf);

  mainLay->addWidget(buildFormCard());
  mainLay->addWidget(buildChartCard(), 1);

  // expected / actual / baseline KPIs in their own window
  m_resultsDialog = new QDialog(this);
  m_resultsDialog->setWindowTitle(tr("Advisor results"));
  auto *dialogLay = new QVBoxLayout(m_resultsDialog);
  dialogLay->setContentsMargins(14 * ssf, 12 * ssf, 14 * ssf, 12 * ssf);
  dialogLay->addWidget(buildMetricsCard());
  m_resultsDialog->resize(static_cast<int>(760 * ssf), m_resultsDialog->sizeHint().height());
}

QWidget *OfflineAdvisorWidget::buildFormCard() {
  double const ssf{utils::ssf()};
  QFrame      *frame = advisorCard(this, {});
  auto        *lay   = static_cast<QVBoxLayout *>(frame->layout());

  auto *row = new QHBoxLayout();
  row->setSpacing(10 * ssf);
  auto addField = [this, frame, row, ssf](QString const &title, double min, double max, int prec, float defaultValue) {
    auto *box    = new QVBoxLayout();
    box->setSpacing(2 * ssf);
    auto *label = new QLabel(title, frame);
    label->setProperty("muted", true);
    auto *edit = new LineEditNum(min, max, prec, false, frame);
    edit->setFont(font());
    edit->setFixedSize(static_cast<int>(120 * ssf), static_cast<int>(32 * ssf));
    edit->setValue(defaultValue);
    box->addWidget(label);
    box->addWidget(edit);
    row->addLayout(box);
    return edit;
  };
  m_startTempEdit  = addField(tr("Start, °C"), -ai::MAX_ABS_TEMP, ai::MAX_ABS_TEMP, 1, 0.0f);
  m_targetTempEdit = addField(tr("Target, °C"), -ai::MAX_ABS_TEMP, ai::MAX_ABS_TEMP, 1, ai::DEFAULT_TARGET_TEMP);
  m_durationEdit   = addField(tr("Duration, s"), 1, 100000, 0, ai::DEFAULT_DURATION_S);
  m_startTempEdit->setToolTip(tr("The chamber temperature now; change it to plan from another start"));

  auto *buttons = new QVBoxLayout();
  buttons->addStretch();
  auto *buttonRow = new QHBoxLayout();
  buttonRow->setSpacing(8 * ssf);
  m_runBtn = new QPushButton(tr("Run experiment"), frame);
  m_runBtn->setObjectName("primaryButton");
  m_stopBtn = new QPushButton(tr("Stop"), frame);
  m_stopBtn->setObjectName("stopButton");
  m_stopBtn->setVisible(false);
  auto *resultsBtn = new QPushButton(tr("Results"), frame);
  resultsBtn->setToolTip(tr("Expected, actual and baseline KPIs of the last experiment"));
  for (QPushButton *button : {m_runBtn, m_stopBtn, resultsBtn}) {
    button->setFixedHeight(static_cast<int>(32 * ssf));
    button->setMinimumWidth(static_cast<int>(130 * ssf));
    buttonRow->addWidget(button);
  }
  buttons->addLayout(buttonRow);
  row->addSpacing(6 * ssf);
  row->addLayout(buttons);

  // status and progress take the rest of the row
  auto *statusBox = new QVBoxLayout();
  statusBox->addStretch();
  m_statusLabel = new QLabel(frame);
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setProperty("muted", true);
  statusBox->addWidget(m_statusLabel);
  m_progressBar = new QProgressBar(frame);
  m_progressBar->setVisible(false);
  m_progressBar->setFixedHeight(static_cast<int>(8 * ssf));
  m_progressBar->setTextVisible(false);
  statusBox->addWidget(m_progressBar);
  row->addSpacing(10 * ssf);
  row->addLayout(statusBox, 1);
  lay->addLayout(row);

  connect(m_runBtn, &QPushButton::clicked, this, &OfflineAdvisorWidget::runExperiment);
  connect(m_stopBtn, &QPushButton::clicked, this, [this] {
    stopExperiment(true, tr("Experiment stopped by user"));
  });
  connect(resultsBtn, &QPushButton::clicked, this, [this] {
    m_resultsDialog->show();
    m_resultsDialog->raise();
    m_resultsDialog->activateWindow();
  });

  return frame;
}

void OfflineAdvisorWidget::updateStartTemp() {
  // the chamber temperature now, or 0 while the chamber is not connected; afterwards the field is the operator's
  bool const   online{m_resHolder->status && (StatusManager::AppState::ONLINE == m_resHolder->status->getAppState())};
  SystemValue *temp = m_resHolder->states->contains("temp") ? m_resHolder->states->getSysValue("temp") : nullptr;
  double const value{(online && temp && std::isfinite(temp->getValue())) ? temp->getValue() : 0.0};
  m_startTempEdit->setValue(static_cast<float>(std::round(value * 10.0) / 10.0));
}

void OfflineAdvisorWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  if (!m_startTempFilled) {
    m_startTempFilled = true;
    updateStartTemp();
  }
}

QWidget *OfflineAdvisorWidget::buildMetricsCard() {
  double const ssf{utils::ssf()};
  auto        *frame = new QWidget(this);
  auto        *frameLay = new QVBoxLayout(frame);
  frameLay->setContentsMargins(0, 0, 0, 0);
  frameLay->setSpacing(8 * ssf);
  auto        *lay   = frameLay;

  m_pidLabel = new QLabel(frame);
  m_pidLabel->setProperty("muted", true);
  m_pidLabel->setWordWrap(true);
  lay->addWidget(m_pidLabel);

  m_metricsTable = new QTableWidget(5, 3, frame);
  m_metricsTable->setObjectName("flatTable");
  m_metricsTable->setHorizontalHeaderLabels({tr("Expected").toUpper(), tr("Actual").toUpper(), tr("Baseline").toUpper()});
  m_metricsTable->setVerticalHeaderLabels(
    {tr("Cost"), tr("Tail MAE, °C"), tr("Overshoot max, °C"), tr("Settle time, s"), tr("Final temp, °C")});
  m_metricsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_metricsTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
  m_metricsTable->horizontalHeader()->setHighlightSections(false);
  m_metricsTable->verticalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_metricsTable->verticalHeader()->setDefaultSectionSize(static_cast<int>(28 * ssf));
  m_metricsTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  m_metricsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_metricsTable->setSelectionMode(QAbstractItemView::NoSelection);
  m_metricsTable->setShowGrid(false);
  m_metricsTable->setFocusPolicy(Qt::NoFocus);
  m_metricsTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_metricsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_metricsTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  int tableHeight = m_metricsTable->horizontalHeader()->sizeHint().height() + 2 * m_metricsTable->frameWidth();
  for (int row{0}; row < m_metricsTable->rowCount(); ++row) {
    tableHeight += m_metricsTable->rowHeight(row);
  }
  m_metricsTable->setFixedHeight(tableHeight + static_cast<int>(4 * ssf));

  lay->addWidget(m_metricsTable);
  lay->addStretch();

  clearMetricsTable();

  return frame;
}

QWidget *OfflineAdvisorWidget::buildChartCard() {
  QFrame *frame = advisorCard(this, tr("Response"));
  auto   *lay   = static_cast<QVBoxLayout *>(frame->layout());

  m_plot = new QCustomPlot(frame);
  m_plot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_plot->setMinimumHeight(220 * utils::ssf());
  m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
  m_plot->xAxis->setLabel(tr("Time, s"));
  m_plot->yAxis->setLabel(tr("Temperature, °C"));

  m_plot->addGraph(); // 0: setpoint
  m_plot->graph(0)->setPen(QPen(QColor(140, 140, 140), 1, Qt::DashLine));
  m_plot->graph(0)->setName(tr("Setpoint"));

  m_plot->addGraph(); // 1: expected (CEM best, twin-predicted before the run)
  m_plot->graph(1)->setPen(QPen(QColor(230, 160, 40), 2, Qt::DashLine));
  m_plot->graph(1)->setName(tr("Expected"));

  m_plot->addGraph(); // 2: actual
  m_plot->graph(2)->setPen(QPen(QColor(40, 100, 200), 2));
  m_plot->graph(2)->setName(tr("Actual"));

  m_plot->addGraph(); // 3: median baseline
  m_plot->graph(3)->setPen(QPen(QColor(40, 170, 90), 2, Qt::DotLine));
  theme::stylePlot(m_plot);
  m_plot->graph(3)->setName(tr("Baseline"));

  // legend: a framed box at the top right
  m_plot->legend->setVisible(true);
  m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignRight | Qt::AlignTop);
  m_plot->legend->setBorderPen(QPen(theme::colors().border));
  m_plot->legend->setBrush(QColor(theme::colors().panel.red(), theme::colors().panel.green(), theme::colors().panel.blue(), 235));

  // With no data yet, QCustomPlot has nothing to rescale the x axis to and
  // its layout pass (which allocates space for the axis label/tick text)
  // never runs until the first replot -- without both, the x axis renders
  // with no visible ticks or label.
  m_plot->xAxis->setRange(0, ai::DEFAULT_DURATION_S);
  m_plot->replot();

  lay->addWidget(m_plot, 1);

  return frame;
}

void OfflineAdvisorWidget::setStatusText(QString const &text, bool isError) {
  m_statusLabel->setText(text);
  m_statusLabel->setStyleSheet(isError ? QStringLiteral("color: %1;").arg(theme::colors().danger.name()) : QString());
}

bool OfflineAdvisorWidget::writePidToActiveBand(double kp, double ki, double kd) {
  QVariantMap const plcSettings = m_resHolder->settings->settingsMap().value("plc_settings").toMap();
  QVariantList const pidsList   = plcSettings.value("pids").toList();
  if (pidsList.isEmpty()) {
    return false;
  }

  QVariantMap    pidMap = pidsList.first().toMap();
  quint32 const  points = pidMap.value("points").toUInt();
  QVariantList   pList  = pidMap.value("p").toList();
  QVariantList   iList  = pidMap.value("i").toList();
  QVariantList   dList  = pidMap.value("d").toList();
  if ((0 == points) || (static_cast<quint32>(pList.size()) != points)
      || (static_cast<quint32>(iList.size()) != points) || (static_cast<quint32>(dList.size()) != points)) {
    return false;
  }

  int           activeIdx{-1};
  SystemValue *idxVal = m_resHolder->states->getSysValue("temp_pid_idx");
  if (idxVal && std::isfinite(idxVal->getValue())) {
    activeIdx = static_cast<int>(std::lround(idxVal->getValue()));
  } else {
    // temp_pid_idx isn't populated until the chamber is actually running
    // (e.g. this write happens before startChamberJob()) -- derive which
    // band the starting temperature falls into from the same min/max/points
    // the PLC itself uses, instead of a stale or NaN live index.
    double const bandMin = pidMap.value("min").toDouble();
    double const bandMax = pidMap.value("max").toDouble();
    double const range   = std::max(bandMax - bandMin, 1e-9);
    activeIdx             = static_cast<int>(std::floor((m_startTemp - bandMin) / range * points));
  }
  activeIdx = std::clamp(activeIdx, 0, static_cast<int>(points) - 1);

  pList[activeIdx] = kp;
  iList[activeIdx] = ki;
  dList[activeIdx] = kd;
  pidMap["p"]      = pList;
  pidMap["i"]      = iList;
  pidMap["d"]      = dList;

  QMap<QString, float> const flatMap = pidVariantToStringMap(pidMap);
  if (flatMap.isEmpty()) {
    return false;
  }

  QVariantList newPidsList = pidsList;
  newPidsList[0]           = pidMap;
  QVariantMap newPlcSettings = plcSettings;
  newPlcSettings["pids"]     = newPidsList;
  m_resHolder->settings->setPlcSettings(newPlcSettings);

  // The chamber echoes back its settings after any write; suppress the
  // "already identical" flag so that expected echo doesn't pop the
  // Settings-page's "Settings match" confirmation dialog mid-experiment.
  if (m_resHolder->status) {
    m_resHolder->status->setSettingsListIdentical(false);
  }
  m_resHolder->tcpGate->sendSettingMap(flatMap);
  return true;
}

bool OfflineAdvisorWidget::startChamberJob(double targetTemp) {
  constexpr quint32 LONG_TIME{100 * 3600 - 1}; // matches Manual::sendProgram(); comfortably exceeds any AI run duration

  QVariantList const controlParamsList = m_resHolder->settings->settingsMap().value("control_params").toList();
  if (controlParamsList.isEmpty()) {
    return false;
  }

  ProgramModel model(controlParamsList, ProgramModel::ProgType::SETPOINT);
  QStringList const paramOrder = model.getParamListOrder();
  int const         tempIdx    = paramOrder.indexOf("temp");
  if (tempIdx < 0) {
    return false;
  }

  QList<QStandardItem *> column;
  for (int i{0}; i < paramOrder.size(); ++i) {
    column.append(new QStandardItem());
  }
  model.getStandardModel()->insertColumn(0, column);

  quint32 const flags = 1u << static_cast<quint32>(tempIdx);

  for (QString const &name : paramOrder) {
    if ("duration" == name) {
      model.setParamValue(0, name, LONG_TIME);
    } else if ("flags" == name) {
      model.setParamValue(0, name, flags);
    } else if ("temp" == name) {
      model.setParamValue(0, name, targetTemp);
    } else {
      SystemValue *live = m_resHolder->states->getSysValue(name);
      model.setParamValue(0, name, live ? live->getValue() : 0.0);
    }
  }

  if (m_resHolder->recorder) {
    m_resHolder->recorder->setNextJob(ExperimentRecorder::JobInfo{
      QStringLiteral("offline_advisor"), tr("AI Advisor"), {}, model.programVariant(),
      QJsonObject{{"target_temp", targetTemp}, {"duration_s", m_targetDurationS}, {"model", "gru_rollout CEM search"}}});
  }
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, model.makeSendData());
  if (m_resHolder->actions) {
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("AI Advisor started the chamber"));
  }
  return true;
}

void OfflineAdvisorWidget::runExperiment() {
  if (m_running || m_tuneWorker) {
    return;
  }
  if (!m_model.isLoaded()) {
    setStatusText(m_model.statusMessage(), true);
    return;
  }
  m_predictionOnly = !m_resHolder->status || (StatusManager::AppState::ONLINE != m_resHolder->status->getAppState());
  if (QString const denied = ai::advisorControlDenied(m_resHolder); !m_predictionOnly && !denied.isEmpty()) {
    setStatusText(denied, true);
    return;
  }
  if (!m_predictionOnly && m_pidGuard->isPending()) {
    setStatusText(tr("The manual PID from the previous experiment has not been confirmed by the chamber yet"), true);
    return;
  }

  double const durationS = m_durationEdit->text().toDouble();
  double const dtS       = ai::DEFAULT_DT_S;
  if (durationS <= 0.0) {
    setStatusText(tr("Invalid scenario parameters"), true);
    return;
  }

  if (!m_predictionOnly && !m_resHolder->states->getSysValue("temp")) {
    setStatusText(tr("Required chamber telemetry not available"), true);
    return;
  }

  m_startTemp       = m_startTempEdit->text().toDouble(); // the chamber temperature unless changed
  m_targetTemp       = m_targetTempEdit->text().toDouble();
  if (!std::isfinite(m_startTemp) || !std::isfinite(m_targetTemp)) {
    setStatusText(tr("Start or target temperature is invalid - cannot tune"), true);
    return;
  }
  if (QString const outside = m_model.envelope().checkScenario(m_startTemp, m_targetTemp, durationS);
      !outside.isEmpty()) {
    setStatusText(outside + tr(" - the digital twin was not trained for it"), true);
    return;
  }
  m_targetDurationS = durationS;

  m_currentParams            = ai::SimParams{};
  m_currentParams.startTemp  = m_startTemp;
  m_currentParams.targetTemp = m_targetTemp;
  m_currentParams.durationS  = durationS;
  m_currentParams.dtS        = dtS;

  m_actualTrajectory.clear();
  clearMetricsTable();
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    m_plot->graph(i)->data()->clear();
  }
  m_plot->replot();

  startTuning();
}

void OfflineAdvisorWidget::startTuning() {
  updateRunButtonEnabled();
  m_stopBtn->setVisible(false);

  int const totalCandidates = ai::CEM_POPULATION * ai::CEM_ITERATIONS;

  double const ssf{utils::ssf()};
  m_tuneDialog = new BusyDialog(this);
  m_tuneDialog->setWindowTitle(tr("Advisor"));
  m_tuneDialog->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
  m_tuneDialog->setWindowModality(Qt::WindowModal);
  auto *lay = new QVBoxLayout(m_tuneDialog);
  lay->setContentsMargins(22 * ssf, 18 * ssf, 22 * ssf, 18 * ssf);
  lay->setSpacing(10 * ssf);
  auto *title = new QLabel(tr("Computing optimal setting with the digital twin..."), m_tuneDialog);
  title->setObjectName("cardTitle");
  title->setWordWrap(true);
  lay->addWidget(title);
  m_tuneBar = new QProgressBar(m_tuneDialog);
  m_tuneBar->setObjectName("slimProgress");
  m_tuneBar->setTextVisible(false);
  m_tuneBar->setFixedHeight(static_cast<int>(8 * ssf));
  m_tuneBar->setRange(0, totalCandidates);
  m_tuneBar->setValue(0);
  lay->addWidget(m_tuneBar);
  auto *info = new QHBoxLayout();
  m_tuneCount = new QLabel(m_tuneDialog);
  m_tuneCount->setProperty("muted", true);
  m_tunePercent = new QLabel(m_tuneDialog);
  m_tunePercent->setProperty("muted", true);
  info->addWidget(m_tuneCount);
  info->addStretch();
  info->addWidget(m_tunePercent);
  lay->addLayout(info);
  onTuneProgress(0, totalCandidates);
  m_tuneDialog->setFixedWidth(static_cast<int>(460 * ssf));
  m_tuneDialog->adjustSize();
  m_tuneDialog->show();

  setStatusText(tr("Running the AI Advisor over the digital twin..."), false);

  m_tuneWorker = new ai::OfflineTuneWorker(&m_model, m_currentParams, this);
  connect(m_tuneWorker, &ai::OfflineTuneWorker::progress, this, &OfflineAdvisorWidget::onTuneProgress);
  connect(m_tuneWorker, &QThread::finished, this, &OfflineAdvisorWidget::onTuneFinished);
  connect(m_tuneWorker, &QThread::finished, m_tuneWorker, &QObject::deleteLater);
  m_tuneWorker->start();
}

void OfflineAdvisorWidget::onTuneProgress(int done, int total) {
  if (m_tuneDialog) {
    m_tuneBar->setMaximum(total);
    m_tuneBar->setValue(done);
    m_tuneCount->setText(tr("%1 of %2 candidates simulated").arg(done).arg(total));
    m_tunePercent->setText(QStringLiteral("%1 %").arg(total > 0 ? (100 * done) / total : 0));
  }
}

void OfflineAdvisorWidget::onTuneFinished() {
  ai::OfflineTuneResult const result = m_tuneWorker->result();
  m_tuneWorker                       = nullptr;

  if (m_tuneDialog) {
    m_tuneDialog->close();
    m_tuneDialog->deleteLater();
    m_tuneDialog = nullptr;
  }

  if (!result.ok) {
    setStatusText(result.errorMessage.isEmpty() ? tr("AI Advisor failed") : result.errorMessage, true);
    updateRunButtonEnabled();
    return;
  }

  m_tuneResult = result;

  if (m_predictionOnly) {
    // no chamber: the suggested coefficients, the predicted experiment and the baseline only
    QVector<double> const setX{0.0, m_targetDurationS};
    QVector<double> const setY{m_targetTemp, m_targetTemp};
    m_plot->graph(0)->setData(setX, setY, true);
    redrawPretunedChart();
    setMetricsColumn(0, m_tuneResult.bestMetrics);
    setMetricsColumn(2, m_tuneResult.medianMetrics);
    m_pidLabel->setText(tr("Suggested PID: %1  |  baseline: %2")
                          .arg(formatPid(m_tuneResult.bestKp, m_tuneResult.bestKi, m_tuneResult.bestKd),
                               formatPid(m_tuneResult.medianKp, m_tuneResult.medianKi, m_tuneResult.medianKd)));
    setStatusText(tr("Prediction only (chamber not connected): suggested %1")
                    .arg(formatPid(m_tuneResult.bestKp, m_tuneResult.bestKi, m_tuneResult.bestKd)),
                  false);
    updateRunButtonEnabled();
    return;
  }

  auto *dialog =
    new ConfDialog(tr("Apply suggested PID?"),
                    tr("AI Advisor suggests %1 for this experiment. Apply it and run?").arg(formatPid(result.bestKp, result.bestKi, result.bestKd)),
                    tr("Yes"), tr("No"), this);
  connect(dialog, &ConfDialog::finished, this, [this](int accepted) {
    if (1 == accepted) { // == accepted()
      // things may have changed while the dialog was open
      QString refusal = ai::advisorControlDenied(m_resHolder);
      if (refusal.isEmpty() && (!m_resHolder->status || (StatusManager::AppState::ONLINE != m_resHolder->status->getAppState()))) {
        refusal = tr("Chamber is not connected");
      }
      if (refusal.isEmpty() && !m_model.envelope().containsPid(m_tuneResult.bestKp, m_tuneResult.bestKi, m_tuneResult.bestKd)) {
        refusal = tr("Suggested PID %1 is outside the valid range")
                    .arg(formatPid(m_tuneResult.bestKp, m_tuneResult.bestKi, m_tuneResult.bestKd));
      }
      if (refusal.isEmpty() && !m_pidGuard->capture()) {
        refusal = tr("No valid manual Temperature PID table to fall back to");
      }
      if (!refusal.isEmpty()) {
        setStatusText(refusal + tr(" - suggested PID not applied"), true);
        updateRunButtonEnabled();
        return;
      }
      if (!writePidToActiveBand(m_tuneResult.bestKp, m_tuneResult.bestKi, m_tuneResult.bestKd)) {
        m_pidGuard->restore(false, tr("suggested PID could not be written"));
        setStatusText(tr("Failed to write suggested PID to the chamber"), true);
        updateRunButtonEnabled();
        return;
      }
      if (!startChamberJob(m_targetTemp)) {
        m_pidGuard->restore(false, tr("chamber could not be started"));
        setStatusText(tr("Failed to start the chamber"), true);
        updateRunButtonEnabled();
        return;
      }
      redrawPretunedChart();
      setMetricsColumn(0, m_tuneResult.bestMetrics);
      setMetricsColumn(2, m_tuneResult.medianMetrics);
      m_pidLabel->setText(tr("Applied PID: %1  |  baseline: %2")
                             .arg(formatPid(m_tuneResult.bestKp, m_tuneResult.bestKi, m_tuneResult.bestKd),
                                  formatPid(m_tuneResult.medianKp, m_tuneResult.medianKi, m_tuneResult.medianKd)));
      startRealRun();
    } else {
      setStatusText(tr("Cancelled - suggested PID not applied"), false);
      updateRunButtonEnabled();
    }
  });
  dialog->open();
}

void OfflineAdvisorWidget::redrawPretunedChart() {
  auto toXY = [](QVector<ai::TrajectoryPoint> const &traj, QVector<double> &xs, QVector<double> &ys) {
    xs.reserve(traj.size());
    ys.reserve(traj.size());
    for (auto const &p : traj) {
      xs << p.elapsedS;
      ys << p.temp;
    }
  };

  QVector<double> expX, expY, medX, medY;
  toXY(m_tuneResult.bestTrajectory, expX, expY);
  toXY(m_tuneResult.medianTrajectory, medX, medY);

  m_plot->graph(1)->setData(expX, expY, true);
  m_plot->graph(3)->setData(medX, medY, true);

  m_plot->rescaleAxes();
  m_plot->yAxis->scaleRange(1.08); // keep the setpoint line off the axis edge
  m_plot->replot();
}

void OfflineAdvisorWidget::startRealRun() {
  m_running = true;
  m_runClock.start();
  m_lastTelemetryMs = 0;
  m_manualRestored  = false;

  m_runBtn->setEnabled(false);
  m_stopBtn->setVisible(true);
  m_progressBar->setVisible(true);
  m_progressBar->setRange(0, static_cast<int>(m_targetDurationS));
  m_progressBar->setValue(0);
  setStatusText(tr("Running real experiment on the chamber..."), false);

  m_progressTimer.start(1000);
}

void OfflineAdvisorWidget::onProgressTick() {
  if (!m_running) {
    return;
  }

  double const elapsedS = m_runClock.elapsed() / 1000.0;
  if (elapsedS >= m_targetDurationS) {
    stopExperiment(false);
    return;
  }
  if (m_runClock.elapsed() - m_lastTelemetryMs > TELEMETRY_TIMEOUT_MS) {
    stopExperiment(true, tr("No chamber telemetry for %1 s - experiment aborted").arg(TELEMETRY_TIMEOUT_MS / 1000));
    return;
  }

  m_progressBar->setValue(static_cast<int>(elapsedS));
  if (m_manualRestored) {
    setStatusText(tr("%1 - continuing on manual PID (%2 / %3 s elapsed)")
                    .arg(m_manualReason)
                    .arg(static_cast<int>(elapsedS))
                    .arg(static_cast<int>(m_targetDurationS)),
                  true);
  } else {
    setStatusText(
      tr("Running real experiment: %1 / %2 s elapsed").arg(static_cast<int>(elapsedS)).arg(static_cast<int>(m_targetDurationS)),
      false);
  }
}

void OfflineAdvisorWidget::onTelemetryUpdated() {
  if (!m_running) {
    return;
  }

  m_lastTelemetryMs = m_runClock.elapsed();

  SystemValue *tempVal = m_resHolder->states->getSysValue("temp");
  if (!tempVal) {
    return;
  }

  ai::TrajectoryPoint point;
  point.elapsedS = m_runClock.elapsed() / 1000.0;
  point.temp     = tempVal->getValue();
  point.tempRef  = m_targetTemp;
  point.error    = point.tempRef - point.temp;

  m_actualTrajectory.push_back(point);
  redrawActualChart();
}

void OfflineAdvisorWidget::onAppStateChanged(StatusManager::AppState state) {
  updateRunButtonEnabled();
  if (m_running && (StatusManager::AppState::ONLINE != state)) {
    stopExperiment(true, tr("Connection lost - experiment aborted"));
  }
}

void OfflineAdvisorWidget::onGateDisconnected() {
  if (m_running) {
    stopExperiment(true, tr("Chamber disconnected - experiment aborted"));
  }
  updateRunButtonEnabled();
}

void OfflineAdvisorWidget::onAlarmReceived(quint32 alarmCode) {
  Q_UNUSED(alarmCode);
  if (m_running) {
    stopExperiment(true, tr("Chamber alarm triggered - experiment aborted"));
  }
}

void OfflineAdvisorWidget::onAccessChanged() {
  updateRunButtonEnabled();
  if (!m_running || m_manualRestored) {
    return;
  }
  if (QString const denied = ai::advisorControlDenied(m_resHolder); !denied.isEmpty()) {
    // the suggested PID was applied on the previous user's authority
    m_pidGuard->restore(false, denied);
    m_manualRestored = true;
    m_manualReason   = denied;
    m_pidLabel->setText(tr("Reverted to manual PID: %1").arg(denied));
  }
}

void OfflineAdvisorWidget::stopExperiment(bool aborted, QString const &reason) {
  if (!m_running) {
    return;
  }
  m_running = false;
  m_progressTimer.stop();
  // manual PID table first, then stop the job the advisor started
  ai::ManualPidGuard::Result const restored =
    m_pidGuard->restore(true, aborted ? (reason.isEmpty() ? tr("experiment aborted") : reason) : tr("experiment finished"));
  QString const restoreNote = (ai::ManualPidGuard::Result::PENDING == restored)
                                ? tr(" - manual PID and stop will be sent when the chamber is reachable again")
                                : QString();

  m_stopBtn->setVisible(false);
  m_progressBar->setVisible(false);
  updateRunButtonEnabled();

  if (aborted) {
    setStatusText((reason.isEmpty() ? tr("Experiment aborted") : reason) + restoreNote, true);
    if (!m_actualTrajectory.isEmpty()) {
      setMetricsColumn(1, computeActualMetrics());
    }
    return;
  }

  setMetricsColumn(1, computeActualMetrics());
  setStatusText(tr("Actual experiment finished"), false);
}

void OfflineAdvisorWidget::redrawActualChart() {
  QVector<double> xs, ys, setX, setY;
  xs.reserve(m_actualTrajectory.size());
  ys.reserve(m_actualTrajectory.size());
  setX.reserve(m_actualTrajectory.size());
  setY.reserve(m_actualTrajectory.size());
  for (auto const &p : m_actualTrajectory) {
    xs << p.elapsedS;
    ys << p.temp;
    setX << p.elapsedS;
    setY << p.tempRef;
  }

  m_plot->graph(0)->setData(setX, setY, true);
  m_plot->graph(2)->setData(xs, ys, true);

  m_plot->rescaleAxes();
  m_plot->replot();
}

ai::SimMetrics OfflineAdvisorWidget::computeActualMetrics() const {
  if (m_actualTrajectory.isEmpty()) {
    ai::SimMetrics metrics;
    metrics.valid = true;
    return metrics;
  }
  // each point against its own live setpoint, so a setpoint change mid-run is judged correctly
  QVector<double> times;
  QVector<double> temps;
  QVector<double> references;
  for (auto const &p : m_actualTrajectory) {
    times << p.elapsedS;
    temps << p.temp;
    references << p.tempRef;
  }
  return ai::metricsFromKpis(
    core::computeKpis(times, temps, references, ai::kpiOptions(m_targetDurationS, m_startTemp)), true);
}

void OfflineAdvisorWidget::setMetricsColumn(int column, ai::SimMetrics const &metrics) {
  auto *costItem   = new QTableWidgetItem(formatNumber(metrics.cost, 2));
  auto *maeItem    = new QTableWidgetItem(formatNumber(metrics.tailMae, 3));
  auto *osItem     = new QTableWidgetItem(formatNumber(metrics.overshootMax, 2));
  auto *settleItem = new QTableWidgetItem(formatNumber(metrics.timeToSettleS, 0));
  auto *endItem    = new QTableWidgetItem(formatNumber(metrics.endTemp, 2));

  for (auto *item : {costItem, maeItem, osItem, settleItem, endItem}) {
    item->setTextAlignment(Qt::AlignCenter);
  }

  m_metricsTable->setItem(0, column, costItem);
  m_metricsTable->setItem(1, column, maeItem);
  m_metricsTable->setItem(2, column, osItem);
  m_metricsTable->setItem(3, column, settleItem);
  m_metricsTable->setItem(4, column, endItem);
}

void OfflineAdvisorWidget::clearMetricsTable() {
  for (int col{0}; col < 3; ++col) {
    for (int row{0}; row < 5; ++row) {
      auto *item = new QTableWidgetItem(QStringLiteral("—"));
      item->setTextAlignment(Qt::AlignCenter);
      m_metricsTable->setItem(row, col, item);
    }
  }
  m_pidLabel->clear();
}

} // namespace deepvaccontrol
