#include "aiadvisorwidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

#include "confdialog.h"
#include "constants.h"
#include "gatetcp.h"
#include "lineeditnum.h"
#include "qcustomplot.h"
#include "settingsmanager.h"
#include "settingstransformer.h"
#include "systemvalue.h"
#include "systemvaluelist.h"
#include "utils.h"

namespace control2 {

namespace {
QString formatNumber(double value, int prec = 2) {
  return QString::number(value, 'f', prec);
}

QString formatPid(double kp, double ki, double kd) {
  return QStringLiteral("Kp=%1  Ki=%2  Kd=%3")
    .arg(formatNumber(kp, 1), formatNumber(ki, 1), formatNumber(kd, 1));
}

constexpr double SAFETY_MAX_OVERSHOOT_C{1.0};
constexpr int    SAFETY_MAX_SETPOINT_CROSSINGS{3};
constexpr double SAFETY_CROSSING_DEADBAND_C{0.2};
// ~4x the GRU's held-out one-step RMSE (foresight validation_rollout_2026-09-02:
// test_one_step.rmse_delta_t1 = 0.056 degC on unseen PID configs).
constexpr double SAFETY_PREDICTION_DRIFT_C{0.2};
constexpr int    SAFETY_PREDICTION_DRIFT_MIN_SAMPLES{3};
constexpr int    SAFETY_PREDICTION_DRIFT_WINDOW{5};
} // namespace

AiAdvisorWidget::AiAdvisorWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  initGui();
  loadModels();

  connect(&m_decisionTimer, &QTimer::timeout, this, &AiAdvisorWidget::onDecisionTimerTick);
  connect(&m_progressTimer, &QTimer::timeout, this, &AiAdvisorWidget::onProgressTick);
  connect(&m_shadowPidTimer, &QTimer::timeout, this, &AiAdvisorWidget::onShadowPidTick);
  connect(&m_twinCheckTimer, &QTimer::timeout, this, &AiAdvisorWidget::onTwinCheckTick);
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &AiAdvisorWidget::onTelemetryUpdated);
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &AiAdvisorWidget::onAppStateChanged);
  connect(m_resHolder->tcpGate, &GateTcp::disconnected, this, &AiAdvisorWidget::onGateDisconnected);
  connect(m_resHolder->tcpGate, &GateTcp::alarmReceived, this, &AiAdvisorWidget::onAlarmReceived);

  updateRunButtonEnabled();
}

AiAdvisorWidget::~AiAdvisorWidget() {
  if (m_running) {
    m_decisionTimer.stop();
    m_progressTimer.stop();
    m_shadowPidTimer.stop();
    m_twinCheckTimer.stop();
  }
  if (m_comparisonWorker) {
    m_comparisonWorker->wait();
  }
}

void AiAdvisorWidget::loadModels() {
  QDir const modelDir(QDir(QCoreApplication::applicationDirPath()).filePath("data/model"));

  bool const gruOk =
    m_model.load(modelDir.filePath("gru_plant_model.onnx"), modelDir.filePath("gru_plant_model.json"));
  bool const advisorOk = m_advisor.load(modelDir.filePath("ppo_advisor.json"));

  bool const ok = gruOk && advisorOk;
  if (!ok) {
    setStatusText(gruOk ? m_advisor.statusMessage() : m_model.statusMessage(), true);
  } else {
    setStatusText(tr("Digital twin and advisor models loaded"), false);
  }
  updateRunButtonEnabled();
}

void AiAdvisorWidget::updateRunButtonEnabled() {
  bool const modelsOk = m_model.isLoaded() && m_advisor.isLoaded();
  bool const online    = m_resHolder->status
                       && (StatusManager::AppState::ONLINE == m_resHolder->status->getAppState());
  m_runBtn->setEnabled(modelsOk && online && !m_running);
}

void AiAdvisorWidget::initGui() {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(4 * utils::ssf(), 4 * utils::ssf(), 4 * utils::ssf(), 4 * utils::ssf());
  mainLay->setSpacing(4 * utils::ssf());

  mainLay->addWidget(buildFormCard());
  mainLay->addWidget(buildChartCard(), 1);
  mainLay->addWidget(buildMetricsCard());
}

QWidget *AiAdvisorWidget::buildFormCard() {
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

  m_durationEdit = addField(0, tr("Duration, s"), 1, 100000, 0, ai::DEFAULT_DURATION_S);
  m_dtEdit       = addField(1, tr("Step, s (for comparison)"), 0.1, 60, 2, ai::DEFAULT_DT_S);

  m_runBtn = new QPushButton(tr("Run experiment"), frame);
  m_runBtn->setFixedHeight(control2::UI_BTN_HEIGHT * utils::ssf());
  lay->addWidget(m_runBtn, 1, 2);

  m_stopBtn = new QPushButton(tr("Stop"), frame);
  m_stopBtn->setFixedHeight(control2::UI_BTN_HEIGHT * utils::ssf());
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

  connect(m_runBtn, &QPushButton::clicked, this, &AiAdvisorWidget::runExperiment);
  connect(m_stopBtn, &QPushButton::clicked, this, [this] {
    stopExperiment(true, tr("Experiment stopped by user"));
  });

  return frame;
}

QWidget *AiAdvisorWidget::buildMetricsCard() {
  auto *frame = new QFrame(this);
  frame->setFrameStyle(QFrame::Box | QFrame::Plain);
  frame->setStyleSheet("QFrame {border: 1px solid #b9b9b9;}");

  auto *lay = new QVBoxLayout(frame);
  lay->setSpacing(2 * utils::ssf());

  m_pidLabel = new QLabel(frame);
  lay->addWidget(m_pidLabel);

  m_metricsTable = new QTableWidget(5, 3, frame);
  m_metricsTable->setHorizontalHeaderLabels({tr("Expected"), tr("Actual"), tr("CEM baseline")});
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

QWidget *AiAdvisorWidget::buildChartCard() {
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

  m_plot->addGraph(); // 1: expected (filled in only after the comparison runs)
  m_plot->graph(1)->setPen(QPen(QColor(230, 160, 40), 2, Qt::DashLine));
  m_plot->graph(1)->setName(tr("Expected"));

  m_plot->addGraph(); // 2: actual
  m_plot->graph(2)->setPen(QPen(QColor(40, 100, 200), 2));
  m_plot->graph(2)->setName(tr("Actual"));

  m_plot->addGraph(); // 3: CEM baseline (filled in only after the comparison runs)
  m_plot->graph(3)->setPen(QPen(QColor(40, 170, 90), 2, Qt::DotLine));
  m_plot->graph(3)->setName(tr("CEM baseline"));

  m_plot->legend->setVisible(true);

  lay->addWidget(m_plot, 1);

  return frame;
}

void AiAdvisorWidget::setStatusText(QString const &text, bool isError) {
  m_statusLabel->setText(text);
  m_statusLabel->setStyleSheet(isError ? "color: #b03030;" : "");
}

std::array<double, 8> AiAdvisorWidget::buildObservation(int decisionIdx) const {
  SystemValue *tempVal    = m_resHolder->states->getSysValue("temp");
  SystemValue *tempRefVal = m_resHolder->states->getSysValue("temp_ref");
  SystemValue *kpVal      = m_resHolder->states->getSysValue("temp_kp");
  SystemValue *kiVal      = m_resHolder->states->getSysValue("temp_ki");
  SystemValue *kdVal      = m_resHolder->states->getSysValue("temp_kd");

  double const temp           = tempVal ? tempVal->getValue() : m_startTemp;
  double const liveTargetTemp = tempRefVal ? tempRefVal->getValue() : m_targetTemp;
  double const kp             = kpVal ? kpVal->getValue() : 0.0;
  double const ki             = kiVal ? kiVal->getValue() : 0.0;
  double const kd             = kdVal ? kdVal->getValue() : 0.0;

  double const scale  = std::max({std::abs(m_advisor.tempMin()), std::abs(m_advisor.tempMax()), 1e-6});
  double const iScale = 1.0; // max(|u_min|, |u_max|, 1e-6) with u_min=-1, u_max=1

  double const nowS    = m_runClock.elapsed() / 1000.0;
  double const dtReal  = std::max(nowS - m_realSecondsAtLastDecision, 1e-6);
  double const velocity = (temp - m_tempAtLastDecision) / dtReal;

  std::array<double, 8> obs{};
  obs[0] = (temp - liveTargetTemp) / scale;
  obs[1] = velocity / scale;
  obs[2] = 2.0 * (kp - m_advisor.kpMin()) / (m_advisor.kpMax() - m_advisor.kpMin()) - 1.0;
  obs[3] = 2.0 * (ki - m_advisor.kiMin()) / (m_advisor.kiMax() - m_advisor.kiMin()) - 1.0;
  obs[4] = 2.0 * (kd - m_advisor.kdMin()) / (m_advisor.kdMax() - m_advisor.kdMin()) - 1.0;
  obs[5] = 1.0 - static_cast<double>(decisionIdx) / static_cast<double>(std::max(1, m_totalDecisions));
  // Exact, not reconstructed: m_shadowPid/m_shadowDiff are the real
  // ChamberPid/CodesysDiff classes, stepped forward every pid_period_s by
  // onShadowPidTick() using the chamber's own real measured
  // temperature/target/gains -- see the class comment in the header.
  obs[6] = m_shadowPid.iPart / iScale;
  obs[7] = m_shadowDiff.filterOut / 5.0;
  return obs;
}

void AiAdvisorWidget::onShadowPidTick() {
  if (!m_running) {
    return;
  }

  SystemValue *tempVal    = m_resHolder->states->getSysValue("temp");
  SystemValue *tempRefVal = m_resHolder->states->getSysValue("temp_ref");
  SystemValue *kpVal      = m_resHolder->states->getSysValue("temp_kp");
  SystemValue *kiVal      = m_resHolder->states->getSysValue("temp_ki");
  SystemValue *kdVal      = m_resHolder->states->getSysValue("temp_kd");
  if (!tempVal || !tempRefVal || !kpVal || !kiVal || !kdVal) {
    return;
  }

  double const temp   = tempVal->getValue();
  double const target = tempRefVal->getValue();
  double const kp     = kpVal->getValue();
  double const ki     = kiVal->getValue();
  double const kd     = kdVal->getValue();

  double const diffOut = m_shadowDiff.update(temp);
  m_shadowPid.step(true, target, temp, kp, ki, kd, diffOut);
}

bool AiAdvisorWidget::writePidToActiveBand(double kp, double ki, double kd) {
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

  SystemValue *idxVal = m_resHolder->states->getSysValue("temp_pid_idx");
  if (!idxVal) {
    return false;
  }
  int activeIdx = static_cast<int>(std::lround(idxVal->getValue()));
  activeIdx     = std::clamp(activeIdx, 0, static_cast<int>(points) - 1);

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

  m_resHolder->tcpGate->sendSettingMap(flatMap);
  return true;
}

void AiAdvisorWidget::captureManualPid() {
  QVariantMap const plcSettings = m_resHolder->settings->settingsMap().value("plc_settings").toMap();
  QVariantList const pidsList   = plcSettings.value("pids").toList();
  if (pidsList.isEmpty()) {
    m_manualPList.clear();
    m_manualIList.clear();
    m_manualDList.clear();
    return;
  }

  QVariantMap const pidMap = pidsList.first().toMap();
  m_manualPList            = pidMap.value("p").toList();
  m_manualIList            = pidMap.value("i").toList();
  m_manualDList            = pidMap.value("d").toList();
}

bool AiAdvisorWidget::lookupManualPidForActiveBand(double &kp, double &ki, double &kd) const {
  int const points = m_manualPList.size();
  if ((0 == points) || (m_manualIList.size() != points) || (m_manualDList.size() != points)) {
    return false;
  }

  SystemValue *idxVal = m_resHolder->states->getSysValue("temp_pid_idx");
  if (!idxVal) {
    return false;
  }
  int activeIdx = static_cast<int>(std::lround(idxVal->getValue()));
  activeIdx     = std::clamp(activeIdx, 0, points - 1);

  bool okP{false}, okI{false}, okD{false};
  kp = m_manualPList.at(activeIdx).toDouble(&okP);
  ki = m_manualIList.at(activeIdx).toDouble(&okI);
  kd = m_manualDList.at(activeIdx).toDouble(&okD);
  return okP && okI && okD;
}

void AiAdvisorWidget::revertToManualPid(QString const &reason) {
  if (m_safetyTripped) {
    return;
  }

  double kp{0.0}, ki{0.0}, kd{0.0};
  if (!lookupManualPidForActiveBand(kp, ki, kd) || !writePidToActiveBand(kp, ki, kd)) {
    stopExperiment(
      true, tr("Safety interlock triggered (%1) but manual PID could not be restored - experiment aborted").arg(reason));
    return;
  }

  m_safetyTripped = true;
  m_safetyReason  = reason;

  m_decisionTimer.stop();
  m_shadowPidTimer.stop();
  m_twinCheckTimer.stop();

  m_decisions.push_back({m_runClock.elapsed() / 1000.0, kp, ki, kd});
  m_pidLabel->setText(tr("Safety interlock: reverted to manual PID %1").arg(formatPid(kp, ki, kd)));
}

void AiAdvisorWidget::checkOvershootAndOscillation(ai::TrajectoryPoint const &point) {
  if (m_safetyTripped) {
    return;
  }

  // Same direction convention as computeActualMetrics(): overshoot is
  // deviation past the setpoint on the wrong side, inferred from whether
  // the run started above or below the live target.
  double overshoot{0.0};
  if (point.tempRef <= m_startTemp) {
    overshoot = std::max(point.tempRef - point.temp, 0.0);
  } else {
    overshoot = std::max(point.temp - point.tempRef, 0.0);
  }
  if (overshoot > SAFETY_MAX_OVERSHOOT_C) {
    revertToManualPid(tr("overshoot of %1 degC exceeds the %2 degC limit")
                         .arg(formatNumber(overshoot, 2), formatNumber(SAFETY_MAX_OVERSHOOT_C, 1)));
    return;
  }

  if (std::abs(point.error) > SAFETY_CROSSING_DEADBAND_C) {
    int const sign = (point.error > 0.0) ? 1 : -1;
    if ((0 != m_lastErrorSign) && (sign != m_lastErrorSign)) {
      ++m_setpointCrossings;
      if (m_setpointCrossings >= SAFETY_MAX_SETPOINT_CROSSINGS) {
        revertToManualPid(tr("setpoint crossed %1 times (oscillating)").arg(m_setpointCrossings));
        return;
      }
    }
    m_lastErrorSign = sign;
  }
}

QVector<float> AiAdvisorWidget::buildLiveFeatureRow(double temp, double tempRef, double previousTemp, double dtS,
                                                     double u, double uP, double uI, double uD, double kp, double ki,
                                                     double kd) const {
  QStringList const &names  = m_model.featureNames();
  double const       error  = tempRef - temp;
  double const       dtSafe = std::max(dtS, 1e-9);

  QVector<float> row(names.size(), 0.0f);
  for (int i{0}; i < names.size(); ++i) {
    QString const &name = names.at(i);
    double          value{0.0};
    if ("temp" == name) {
      value = temp;
    } else if ("temp_ref" == name) {
      value = tempRef;
    } else if ("error" == name) {
      value = error;
    } else if ("temp_u" == name) {
      value = u;
    } else if ("temp_u_p" == name) {
      value = uP;
    } else if ("temp_u_i" == name) {
      value = uI;
    } else if ("temp_u_d" == name) {
      value = uD;
    } else if ("kp" == name) {
      value = kp;
    } else if ("ki" == name) {
      value = ki;
    } else if ("kd" == name) {
      value = kd;
    } else if ("abs_error" == name) {
      value = std::abs(error);
    } else if ("dt_s" == name) {
      value = dtS;
    } else if ("temp_velocity" == name) {
      value = (temp - previousTemp) / dtSafe;
    } else if ("error_velocity" == name) {
      value = -(temp - previousTemp) / dtSafe;
    }
    row[i] = static_cast<float>(value);
  }
  return row;
}

void AiAdvisorWidget::onTwinCheckTick() {
  if (!m_running || m_safetyTripped || !m_model.isLoaded()) {
    return;
  }

  SystemValue *tempVal    = m_resHolder->states->getSysValue("temp");
  SystemValue *tempRefVal = m_resHolder->states->getSysValue("temp_ref");
  SystemValue *kpVal      = m_resHolder->states->getSysValue("temp_kp");
  SystemValue *kiVal      = m_resHolder->states->getSysValue("temp_ki");
  SystemValue *kdVal      = m_resHolder->states->getSysValue("temp_kd");
  SystemValue *uVal       = m_resHolder->states->getSysValue("temp_u");
  SystemValue *uPVal      = m_resHolder->states->getSysValue("temp_u_p");
  SystemValue *uIVal      = m_resHolder->states->getSysValue("temp_u_i");
  SystemValue *uDVal      = m_resHolder->states->getSysValue("temp_u_d");
  if (!tempVal || !tempRefVal || !kpVal || !kiVal || !kdVal) {
    return;
  }

  double const temp    = tempVal->getValue();
  double const tempRef = tempRefVal->getValue();
  double const dtS     = m_currentParams.dtS; // matches the twin-check timer's fixed cadence

  int const windowSteps = m_model.windowSteps();
  int const nFeatures   = m_model.featureNames().size();
  if ((windowSteps <= 0) || (nFeatures <= 0)) {
    return;
  }

  // Score the prediction made on the previous tick (for "now") against
  // what actually happened, before folding this tick's real row in.
  if (m_liveWindowReady && m_hasPendingPrediction) {
    double const predictionError = std::abs(temp - m_pendingPredictedTemp);
    m_recentPredictionErrors.push_back(predictionError);
    while (m_recentPredictionErrors.size() > SAFETY_PREDICTION_DRIFT_WINDOW) {
      m_recentPredictionErrors.removeFirst();
    }
    if (m_recentPredictionErrors.size() >= SAFETY_PREDICTION_DRIFT_MIN_SAMPLES) {
      double sum{0.0};
      for (double const e : m_recentPredictionErrors) {
        sum += e;
      }
      double const rollingMae = sum / m_recentPredictionErrors.size();
      if (rollingMae > SAFETY_PREDICTION_DRIFT_C) {
        revertToManualPid(tr("digital twin prediction drifted %1 degC from reality (limit %2 degC)")
                             .arg(formatNumber(rollingMae, 2), formatNumber(SAFETY_PREDICTION_DRIFT_C, 2)));
        return;
      }
    }
  }

  QVector<float> const row =
    buildLiveFeatureRow(temp, tempRef, m_liveWindowReady ? m_liveWindowPreviousTemp : temp, dtS,
                         uVal ? uVal->getValue() : 0.0, uPVal ? uPVal->getValue() : 0.0,
                         uIVal ? uIVal->getValue() : 0.0, uDVal ? uDVal->getValue() : 0.0, kpVal->getValue(),
                         kiVal->getValue(), kdVal->getValue());

  if (!m_liveWindowReady) {
    m_liveFeatureWindow.clear();
    m_liveFeatureWindow.reserve(windowSteps * nFeatures);
    for (int i{0}; i < windowSteps; ++i) {
      m_liveFeatureWindow += row;
    }
    m_liveWindowReady = true;
  } else {
    m_liveFeatureWindow = m_liveFeatureWindow.mid(nFeatures) + row;
  }

  float predDelta{0.0f};
  if (m_model.predictDeltaT1(m_liveFeatureWindow, predDelta)) {
    m_pendingPredictedTemp = temp + static_cast<double>(predDelta);
    m_hasPendingPrediction = true;
  } else {
    m_hasPendingPrediction = false;
  }

  m_liveWindowPreviousTemp = temp;
}

void AiAdvisorWidget::runExperiment() {
  if (m_running) {
    return;
  }
  if (!m_model.isLoaded() || !m_advisor.isLoaded()) {
    setStatusText(m_model.isLoaded() ? m_advisor.statusMessage() : m_model.statusMessage(), true);
    return;
  }
  if (!m_resHolder->status || (StatusManager::AppState::ONLINE != m_resHolder->status->getAppState())) {
    setStatusText(tr("Chamber is not connected"), true);
    return;
  }

  double const durationS = m_durationEdit->text().toDouble();
  double const dtS       = m_dtEdit->text().toDouble();
  if ((durationS <= 0.0) || (dtS <= 0.0)) {
    setStatusText(tr("Invalid scenario parameters"), true);
    return;
  }

  SystemValue *tempVal    = m_resHolder->states->getSysValue("temp");
  SystemValue *tempRefVal = m_resHolder->states->getSysValue("temp_ref");
  if (!tempVal || !tempRefVal) {
    setStatusText(tr("Required chamber telemetry not available"), true);
    return;
  }

  m_startTemp       = tempVal->getValue();
  m_targetTemp       = tempRefVal->getValue();
  m_targetDurationS = durationS;

  m_currentParams             = ai::SimParams{};
  m_currentParams.startTemp   = m_startTemp;
  m_currentParams.targetTemp  = m_targetTemp;
  m_currentParams.durationS   = durationS;
  m_currentParams.dtS         = dtS;

  m_decisions.clear();
  m_actualTrajectory.clear();
  clearMetricsTable();
  for (int i{0}; i < m_plot->graphCount(); ++i) {
    m_plot->graph(i)->data()->clear();
  }
  m_plot->replot();

  double const holdS = m_advisor.mpcHoldS();
  m_totalDecisions    = std::max(1, static_cast<int>(std::ceil(durationS / holdS)));

  // Cold-start the shadow PID/Diff, same convention as ChamberTwinSim::begin().
  m_shadowPid              = ai::ChamberPid(ai::U_MIN, ai::U_MAX, ai::PID_I_REVERSE_MUL);
  m_shadowPid.pPart        = 0.0;
  m_shadowPid.iPart        = 0.0;
  m_shadowPid.dPart        = 0.0;
  m_shadowDiff             = ai::CodesysDiff();
  m_shadowDiff.prevValue   = m_startTemp;
  m_shadowDiff.filterOut   = 0.0;
  m_shadowDiff.out         = 0.0;

  m_decisionIdx               = 0;
  m_tempAtLastDecision         = m_startTemp;
  m_realSecondsAtLastDecision = 0.0;

  m_safetyTripped = false;
  m_safetyReason.clear();
  m_setpointCrossings = 0;
  m_lastErrorSign      = 0;
  m_liveFeatureWindow.clear();
  m_liveWindowReady      = false;
  m_hasPendingPrediction = false;
  m_pendingPredictedTemp = 0.0;
  m_recentPredictionErrors.clear();
  captureManualPid();

  m_running = true;
  m_runClock.start();

  m_runBtn->setEnabled(false);
  m_stopBtn->setVisible(true);
  m_progressBar->setVisible(true);
  m_progressBar->setRange(0, static_cast<int>(durationS));
  m_progressBar->setValue(0);
  setStatusText(tr("Running real experiment on the chamber..."), false);

  onDecisionTimerTick(); // first decision, immediately

  m_decisionTimer.start(static_cast<int>(holdS * 1000.0));
  m_progressTimer.start(1000);
  m_shadowPidTimer.start(static_cast<int>(ai::PID_PERIOD_S * 1000.0));
  m_twinCheckTimer.start(static_cast<int>(std::max(dtS, 0.1) * 1000.0));
}

void AiAdvisorWidget::onDecisionTimerTick() {
  if (!m_running) {
    return;
  }

  std::array<double, 8> const obs = buildObservation(m_decisionIdx);
  std::array<double, 3> const pid = m_advisor.decide(obs);

  m_tempAtLastDecision         = m_resHolder->states->getSysValue("temp")->getValue();
  m_realSecondsAtLastDecision = m_runClock.elapsed() / 1000.0;

  if (!writePidToActiveBand(pid[0], pid[1], pid[2])) {
    stopExperiment(true, tr("Failed to write PID to the chamber"));
    return;
  }

  m_decisions.push_back({m_realSecondsAtLastDecision, pid[0], pid[1], pid[2]});
  m_pidLabel->setText(tr("Last written PID: %1").arg(formatPid(pid[0], pid[1], pid[2])));
  ++m_decisionIdx;
}

void AiAdvisorWidget::onProgressTick() {
  if (!m_running) {
    return;
  }

  double const elapsedS = m_runClock.elapsed() / 1000.0;
  if (elapsedS >= m_targetDurationS) {
    stopExperiment(false);
    return;
  }

  m_progressBar->setValue(static_cast<int>(elapsedS));
  if (m_safetyTripped) {
    setStatusText(tr("SAFETY INTERLOCK: %1 - AI suspended, continuing on manual PID (%2 / %3 s elapsed)")
                    .arg(m_safetyReason)
                    .arg(static_cast<int>(elapsedS))
                    .arg(static_cast<int>(m_targetDurationS)),
                  true);
  } else {
    setStatusText(
      tr("Running real experiment: %1 / %2 s elapsed").arg(static_cast<int>(elapsedS)).arg(static_cast<int>(m_targetDurationS)),
      false);
  }
}

void AiAdvisorWidget::onTelemetryUpdated() {
  if (!m_running) {
    return;
  }

  SystemValue *tempVal    = m_resHolder->states->getSysValue("temp");
  SystemValue *tempRefVal = m_resHolder->states->getSysValue("temp_ref");
  if (!tempVal || !tempRefVal) {
    return;
  }
  SystemValue *uVal  = m_resHolder->states->getSysValue("temp_u");
  SystemValue *uPVal = m_resHolder->states->getSysValue("temp_u_p");
  SystemValue *uIVal = m_resHolder->states->getSysValue("temp_u_i");
  SystemValue *uDVal = m_resHolder->states->getSysValue("temp_u_d");

  ai::TrajectoryPoint point;
  point.elapsedS  = m_runClock.elapsed() / 1000.0;
  point.temp      = tempVal->getValue();
  point.tempRef   = tempRefVal->getValue();
  point.error     = point.tempRef - point.temp;
  point.u         = uVal ? uVal->getValue() : 0.0;
  point.uP        = uPVal ? uPVal->getValue() : 0.0;
  point.uI        = uIVal ? uIVal->getValue() : 0.0;
  point.uD        = uDVal ? uDVal->getValue() : 0.0;
  point.predDelta = 0.0;

  m_actualTrajectory.push_back(point);
  redrawActualChart();
  checkOvershootAndOscillation(point);
}

void AiAdvisorWidget::onAppStateChanged(StatusManager::AppState state) {
  updateRunButtonEnabled();
  if (m_running && (StatusManager::AppState::ONLINE != state)) {
    stopExperiment(true, tr("Connection lost - experiment aborted"));
  }
}

void AiAdvisorWidget::onGateDisconnected() {
  if (m_running) {
    stopExperiment(true, tr("Chamber disconnected - experiment aborted"));
  }
  updateRunButtonEnabled();
}

void AiAdvisorWidget::onAlarmReceived(quint32 alarmCode) {
  Q_UNUSED(alarmCode);
  if (m_running) {
    stopExperiment(true, tr("Chamber alarm triggered - experiment aborted"));
  }
}

void AiAdvisorWidget::stopExperiment(bool aborted, QString const &reason) {
  if (!m_running) {
    return;
  }
  m_running = false;
  m_decisionTimer.stop();
  m_progressTimer.stop();
  m_shadowPidTimer.stop();
  m_twinCheckTimer.stop();

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
  if (m_safetyTripped) {
    setStatusText(tr("Actual run finished - safety interlock was triggered: %1").arg(m_safetyReason), true);
  } else {
    setStatusText(tr("Actual run finished"), false);
  }
  promptForComparison();
}

void AiAdvisorWidget::promptForComparison() {
  auto *dialog = new ConfDialog(tr("Compare without Digital Twin?"),
                                 tr("The run has finished. Compare the result without using the Digital Twin?"),
                                 tr("Yes"), tr("No"), this);
  connect(dialog, &ConfDialog::finished, this, [this](int result) {
    if (1 == result) { // == accepted()
      runComparison();
    } else {
      setStatusText(tr("Showing actual run only"), false);
    }
  });
  dialog->open();
}

void AiAdvisorWidget::runComparison() {
  m_progressBar->setVisible(true);
  m_progressBar->setRange(0, 1);
  m_progressBar->setValue(0);
  setStatusText(tr("Computing expected behavior and CEM baseline..."), false);

  m_comparisonWorker = new ai::ComparisonWorker(&m_model, m_currentParams, m_decisions, this);
  connect(m_comparisonWorker, &ai::ComparisonWorker::progress, this, &AiAdvisorWidget::onComparisonProgress);
  connect(m_comparisonWorker, &QThread::finished, this, &AiAdvisorWidget::onComparisonFinished);
  connect(m_comparisonWorker, &QThread::finished, m_comparisonWorker, &QObject::deleteLater);
  m_comparisonWorker->start();
}

void AiAdvisorWidget::onComparisonProgress(int done, int total) {
  m_progressBar->setRange(0, total);
  m_progressBar->setValue(done);
  setStatusText(tr("Running CEM baseline search: %1 / %2 candidates").arg(done).arg(total), false);
}

void AiAdvisorWidget::onComparisonFinished() {
  ai::ComparisonResult const result = m_comparisonWorker->result();
  m_comparisonWorker                = nullptr;

  m_progressBar->setVisible(false);

  if (!result.ok) {
    setStatusText(result.errorMessage.isEmpty() ? tr("Comparison failed") : result.errorMessage, true);
    return;
  }

  setStatusText(tr("Done"), false);
  redrawComparisonChart(result);
  setMetricsColumn(0, result.expectedMetrics);
  setMetricsColumn(2, result.cemMetrics);
}

void AiAdvisorWidget::redrawActualChart() {
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

void AiAdvisorWidget::redrawComparisonChart(ai::ComparisonResult const &result) {
  auto toXY = [](QVector<ai::TrajectoryPoint> const &traj, QVector<double> &xs, QVector<double> &ys) {
    xs.reserve(traj.size());
    ys.reserve(traj.size());
    for (auto const &p : traj) {
      xs << p.elapsedS;
      ys << p.temp;
    }
  };

  QVector<double> expX, expY, cemX, cemY;
  toXY(result.expectedTrajectory, expX, expY);
  toXY(result.cemTrajectory, cemX, cemY);

  m_plot->graph(1)->setData(expX, expY, true);
  m_plot->graph(3)->setData(cemX, cemY, true);

  m_plot->rescaleAxes();
  m_plot->replot();

  m_pidLabel->setText(tr("CEM baseline triplet: %1").arg(formatPid(result.cemKp, result.cemKi, result.cemKd)));
}

ai::SimMetrics AiAdvisorWidget::computeActualMetrics() const {
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

    // Uses each point's own live setpoint (p.tempRef), not the value
    // captured at run start, so a setpoint change mid-run is reflected
    // immediately rather than judged against a stale target.
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

void AiAdvisorWidget::setMetricsColumn(int column, ai::SimMetrics const &metrics) {
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

void AiAdvisorWidget::clearMetricsTable() {
  for (int col{0}; col < 3; ++col) {
    for (int row{0}; row < 5; ++row) {
      auto *item = new QTableWidgetItem(QStringLiteral("—"));
      item->setTextAlignment(Qt::AlignCenter);
      m_metricsTable->setItem(row, col, item);
    }
  }
  m_pidLabel->clear();
}

} // namespace control2
