#include "offlineadvisorwidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

#include "actionmanager.h"
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
  return QString::number(value, 'f', prec);
}

QString formatPid(double kp, double ki, double kd) {
  return QStringLiteral("Kp=%1  Ki=%2  Kd=%3")
    .arg(formatNumber(kp, 1), formatNumber(ki, 1), formatNumber(kd, 1));
}
} // namespace

OfflineAdvisorWidget::OfflineAdvisorWidget(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder} {
  initGui();
  loadModel();

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
  }
  if (m_tuneWorker) {
    m_tuneWorker->wait();
  }
}

void OfflineAdvisorWidget::loadModel() {
  QDir const modelDir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));

  bool const ok = m_model.load(modelDir.filePath("gru_rollout.onnx"), modelDir.filePath("gru_rollout.json"));
  setStatusText(ok ? tr("Digital twin loaded") : m_model.statusMessage(), !ok);
  updateRunButtonEnabled();
}

void OfflineAdvisorWidget::updateRunButtonEnabled() {
  bool const online = m_resHolder->status && (StatusManager::AppState::ONLINE == m_resHolder->status->getAppState());
  m_runBtn->setEnabled(m_model.isLoaded() && online && !m_running && !m_tuneWorker);
}

void OfflineAdvisorWidget::initGui() {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(4 * utils::ssf(), 4 * utils::ssf(), 4 * utils::ssf(), 4 * utils::ssf());
  mainLay->setSpacing(4 * utils::ssf());

  mainLay->addWidget(buildFormCard());
  mainLay->addWidget(buildChartCard(), 1);
  mainLay->addWidget(buildMetricsCard());
}

QWidget *OfflineAdvisorWidget::buildFormCard() {
  auto *frame = new QFrame(this);
  frame->setFrameStyle(QFrame::Box | QFrame::Plain);
  frame->setStyleSheet("QFrame {border: 1px solid #b9b9b9;}");

  auto *lay = new QGridLayout(frame);
  lay->setSpacing(3 * utils::ssf());

  auto addField = [this, lay](int col, QString const &title, double min, double max, int prec, float defaultValue) {
    auto *label = new QLabel(title, lay->parentWidget());
    auto *edit  = new LineEditNum(min, max, prec, false, lay->parentWidget());
    edit->setFixedWidth(110 * utils::ssf());
    edit->setValue(defaultValue);
    lay->addWidget(label, 0, col, Qt::AlignHCenter);
    lay->addWidget(edit, 1, col);
    return edit;
  };

  m_durationEdit   = addField(0, tr("Duration, s"), 1, 100000, 0, ai::DEFAULT_DURATION_S);
  m_targetTempEdit = addField(1, tr("Target temp, °C"), -ai::MAX_ABS_TEMP, ai::MAX_ABS_TEMP, 1, ai::DEFAULT_TARGET_TEMP);

  m_runBtn = new QPushButton(tr("Run experiment"), frame);
  m_runBtn->setFixedHeight(deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  lay->addWidget(m_runBtn, 1, 2);

  m_stopBtn = new QPushButton(tr("Stop"), frame);
  m_stopBtn->setFixedHeight(deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  m_stopBtn->setStyleSheet("QPushButton { background-color: #c0392b; color: white; }");
  m_stopBtn->setVisible(false);
  lay->addWidget(m_stopBtn, 1, 3);

  m_statusLabel = new QLabel(frame);
  m_statusLabel->setWordWrap(true);
  lay->addWidget(m_statusLabel, 1, 4);

  m_progressBar = new QProgressBar(frame);
  m_progressBar->setVisible(false);
  lay->addWidget(m_progressBar, 2, 0, 1, 5);

  lay->setColumnStretch(4, 1);

  connect(m_runBtn, &QPushButton::clicked, this, &OfflineAdvisorWidget::runExperiment);
  connect(m_stopBtn, &QPushButton::clicked, this, [this] {
    stopExperiment(true, tr("Experiment stopped by user"));
  });

  return frame;
}

QWidget *OfflineAdvisorWidget::buildMetricsCard() {
  auto *frame = new QFrame(this);
  frame->setFrameStyle(QFrame::Box | QFrame::Plain);
  frame->setStyleSheet("QFrame {border: 1px solid #b9b9b9;}");

  auto *lay = new QVBoxLayout(frame);
  lay->setSpacing(2 * utils::ssf());

  m_pidLabel = new QLabel(frame);
  lay->addWidget(m_pidLabel);

  m_metricsTable = new QTableWidget(5, 3, frame);
  m_metricsTable->setHorizontalHeaderLabels({tr("Expected"), tr("Actual"), tr("Median baseline")});
  m_metricsTable->setVerticalHeaderLabels(
    {tr("Cost"), tr("Tail MAE"), tr("Overshoot max, °C"), tr("Settle time, s"), tr("Final temp, °C")});
  m_metricsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_metricsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_metricsTable->setSelectionMode(QAbstractItemView::NoSelection);
  m_metricsTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_metricsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_metricsTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  int tableHeight = m_metricsTable->horizontalHeader()->height() + 2 * m_metricsTable->frameWidth();
  for (int row{0}; row < m_metricsTable->rowCount(); ++row) {
    tableHeight += m_metricsTable->rowHeight(row);
  }
  m_metricsTable->setFixedHeight(tableHeight);

  lay->addWidget(m_metricsTable);

  clearMetricsTable();

  return frame;
}

QWidget *OfflineAdvisorWidget::buildChartCard() {
  auto *frame = new QFrame(this);
  frame->setFrameStyle(QFrame::Box | QFrame::Plain);
  frame->setStyleSheet("QFrame {border: 1px solid #b9b9b9;}");

  auto *lay = new QVBoxLayout(frame);
  lay->setContentsMargins(2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf());

  m_plot = new QCustomPlot(frame);
  m_plot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_plot->setMinimumHeight(320 * utils::ssf());
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
  m_plot->graph(3)->setName(tr("Median baseline"));

  m_plot->legend->setVisible(true);

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
  m_statusLabel->setStyleSheet(isError ? "color: #b03030;" : "");
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

  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::JOB, model.makeSendData());
  if (m_resHolder->actions) {
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("AI Advisor started the chamber"));
  }
  return true;
}

void OfflineAdvisorWidget::stopChamberJob() {
  m_resHolder->tcpGate->sendTextCmd("stop");
  if (m_resHolder->actions) {
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("AI Advisor stopped the chamber"));
  }
}

void OfflineAdvisorWidget::runExperiment() {
  if (m_running || m_tuneWorker) {
    return;
  }
  if (!m_model.isLoaded()) {
    setStatusText(m_model.statusMessage(), true);
    return;
  }
  if (!m_resHolder->status || (StatusManager::AppState::ONLINE != m_resHolder->status->getAppState())) {
    setStatusText(tr("Chamber is not connected"), true);
    return;
  }

  double const durationS = m_durationEdit->text().toDouble();
  double const dtS       = ai::DEFAULT_DT_S;
  if (durationS <= 0.0) {
    setStatusText(tr("Invalid scenario parameters"), true);
    return;
  }

  SystemValue *tempVal = m_resHolder->states->getSysValue("temp");
  if (!tempVal) {
    setStatusText(tr("Required chamber telemetry not available"), true);
    return;
  }

  m_startTemp       = tempVal->getValue();
  m_targetTemp       = m_targetTempEdit->text().toDouble();
  if (!std::isfinite(m_startTemp) || !std::isfinite(m_targetTemp)) {
    setStatusText(tr("Chamber temperature reading is invalid - cannot tune"), true);
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

  m_tuneDialog = new QProgressDialog(tr("Computing optimal PID coefficients with the digital twin..."), QString(), 0,
                                      totalCandidates, this);
  m_tuneDialog->setWindowTitle(tr("Offline advisor"));
  m_tuneDialog->setWindowModality(Qt::WindowModal);
  m_tuneDialog->setMinimumDuration(0);
  m_tuneDialog->setCancelButton(nullptr);
  m_tuneDialog->setValue(0);
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
    m_tuneDialog->setMaximum(total);
    m_tuneDialog->setValue(done);
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

  auto *dialog =
    new ConfDialog(tr("Apply suggested PID?"),
                    tr("AI Advisor suggests %1 for this experiment. Apply it and run?").arg(formatPid(result.bestKp, result.bestKi, result.bestKd)),
                    tr("Yes"), tr("No"), this);
  connect(dialog, &ConfDialog::finished, this, [this](int accepted) {
    if (1 == accepted) { // == accepted()
      if (!writePidToActiveBand(m_tuneResult.bestKp, m_tuneResult.bestKi, m_tuneResult.bestKd)) {
        setStatusText(tr("Failed to write suggested PID to the chamber"), true);
        updateRunButtonEnabled();
        return;
      }
      if (!startChamberJob(m_targetTemp)) {
        setStatusText(tr("Failed to start the chamber"), true);
        updateRunButtonEnabled();
        return;
      }
      redrawPretunedChart();
      setMetricsColumn(0, m_tuneResult.bestMetrics);
      setMetricsColumn(2, m_tuneResult.medianMetrics);
      m_pidLabel->setText(tr("Applied PID: %1  |  median baseline: %2")
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
  m_plot->replot();
}

void OfflineAdvisorWidget::startRealRun() {
  m_running = true;
  m_runClock.start();

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

  m_progressBar->setValue(static_cast<int>(elapsedS));
  setStatusText(
    tr("Running real experiment: %1 / %2 s elapsed").arg(static_cast<int>(elapsedS)).arg(static_cast<int>(m_targetDurationS)),
    false);
}

void OfflineAdvisorWidget::onTelemetryUpdated() {
  if (!m_running) {
    return;
  }

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

void OfflineAdvisorWidget::stopExperiment(bool aborted, QString const &reason) {
  if (!m_running) {
    return;
  }
  m_running = false;
  m_progressTimer.stop();
  stopChamberJob();

  m_runBtn->setEnabled(true);
  m_stopBtn->setVisible(false);
  m_progressBar->setVisible(false);
  updateRunButtonEnabled();

  if (aborted) {
    setStatusText(reason.isEmpty() ? tr("Experiment aborted") : reason, true);
    if (!m_actualTrajectory.isEmpty()) {
      setMetricsColumn(1, computeActualMetrics());
    }
    return;
  }

  setMetricsColumn(1, computeActualMetrics());
  setStatusText(tr("Actual run finished"), false);
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
  ai::SimMetrics metrics;
  metrics.valid = true;

  if (m_actualTrajectory.isEmpty()) {
    return metrics;
  }

  double const tailStart = std::max(0.0, m_targetDurationS - ai::TAIL_WINDOW_S);
  bool         anyTail{false};
  for (auto const &p : m_actualTrajectory) {
    if (p.elapsedS >= tailStart) {
      anyTail = true;
      break;
    }
  }

  double sumAbsErrorFull{0.0};
  double sumTailAbsError{0.0};
  double sumTailError{0.0};
  double tailTempSum{0.0};
  int    tailCount{0};
  double minTemp{std::numeric_limits<double>::infinity()};
  double maxTemp{-std::numeric_limits<double>::infinity()};
  double overshootMax{0.0};
  double sumOvershootSq{0.0};
  double timeToNear{m_targetDurationS + 999.0};
  double timeToSettle{m_targetDurationS + 999.0};
  bool   foundNear{false};
  bool   foundSettle{false};

  for (auto const &p : m_actualTrajectory) {
    double const absErr = std::abs(p.error);
    sumAbsErrorFull += absErr;
    minTemp = std::min(minTemp, p.temp);
    maxTemp = std::max(maxTemp, p.temp);

    double overshoot{0.0};
    if (p.tempRef <= m_startTemp) {
      overshoot = std::max(p.tempRef - p.temp, 0.0);
    } else {
      overshoot = std::max(p.temp - p.tempRef, 0.0);
    }
    overshootMax = std::max(overshootMax, overshoot);
    sumOvershootSq += overshoot * overshoot;

    bool const inTail = anyTail ? (p.elapsedS >= tailStart) : true;
    if (inTail) {
      sumTailAbsError += absErr;
      sumTailError += p.error;
      tailTempSum += p.temp;
      ++tailCount;
    }

    if (!foundNear && (absErr <= ai::NEAR_BAND)) {
      timeToNear = p.elapsedS;
      foundNear  = true;
    }
    if (!foundSettle && (absErr <= ai::SETTLE_BAND)) {
      timeToSettle = p.elapsedS;
      foundSettle  = true;
    }
  }

  int const    n        = m_actualTrajectory.size();
  double const tailMean = tailCount > 0 ? tailTempSum / tailCount : 0.0;
  double       sumTailVar{0.0};
  if (tailCount > 0) {
    for (auto const &p : m_actualTrajectory) {
      bool const inTail = anyTail ? (p.elapsedS >= tailStart) : true;
      if (inTail) {
        double const d = p.temp - tailMean;
        sumTailVar += d * d;
      }
    }
  }

  metrics.maeFull        = sumAbsErrorFull / n;
  metrics.tailMae        = tailCount > 0 ? sumTailAbsError / tailCount : 0.0;
  metrics.tailBias       = tailCount > 0 ? sumTailError / tailCount : 0.0;
  metrics.tailStd        = tailCount > 0 ? std::sqrt(sumTailVar / tailCount) : 0.0;
  metrics.overshootMax   = overshootMax;
  metrics.overshootRmse  = std::sqrt(sumOvershootSq / n);
  metrics.finalError     = m_actualTrajectory.last().error;
  metrics.finalAbsError  = std::abs(metrics.finalError);
  metrics.timeToNearS    = timeToNear;
  metrics.timeToSettleS  = timeToSettle;
  metrics.minTemp        = minTemp;
  metrics.maxTemp        = maxTemp;
  metrics.endTemp        = m_actualTrajectory.last().temp;

  metrics.cost = ai::W_TAIL_MAE * metrics.tailMae + ai::W_OVERSHOOT_MAX * metrics.overshootMax
               + ai::W_TAIL_STD * metrics.tailStd + ai::W_FINAL_ERROR * metrics.finalAbsError;

  return metrics;
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
