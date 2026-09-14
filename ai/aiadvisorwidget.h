#ifndef AIADVISORWIDGET_H
#define AIADVISORWIDGET_H

#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <QWidget>
#include <array>

#include "advisorexperiment.h"
#include "chambertwinsim.h"
#include "gruonnxmodel.h"
#include "ppoadvisormodel.h"
#include "resholder.h"
#include "statusmanager.h"

class QCustomPlot;
class QProgressBar;
class QPushButton;
class QTableWidget;

namespace control2 {
class LineEditNum;

// Advisor tab of the AI page.
//
// Run experiment drives the REAL chamber: the trained PPO advisor
// re-decides the PID triplet every mpc_hold_s of real wall-clock time from
// live telemetry (ResHolder::states), and writes it to the currently-active
// band of the chamber's gain-scheduled PID table only (settings.json's
// plc_settings.pids / the live temp_pid_idx state) via the same
// pidVariantToStringMap()+GateTcp::sendSettingMap() path the Settings page
// uses, without touching general settings/calibrations/alarms. Every
// decision (sim time + chosen PID) is recorded as it happens. This only
// runs while the app reports AppState::ONLINE; a Stop button aborts it
// manually, a dropped connection or an incoming alarm aborts it
// automatically -- in both cases the last-written PID is simply left as-is.
//
// Once that run finishes (or is stopped), a modal asks whether to compare
// it against purely-simulated curves from the digital twin: Expected (the
// exact recorded decision log replayed against a fresh twin instance) and
// a CEM-search baseline for a single best fixed PID triplet (foresight's
// advise_pid.py --mode suggest). See advisorexperiment.h for that part,
// which is unchanged -- still simulation-only, now compared against the
// real run instead of a simulated one.
//
// A safety interlock runs alongside the advisor for the whole run: it
// watches live telemetry for excess overshoot, setpoint oscillation, and a
// live one-step-ahead digital-twin prediction that has drifted too far from
// reality. The first condition to trip reverts the active gain-scheduled
// band to its pre-experiment ("manual") PID, raises a persistent warning,
// and stops trusting further advisor decisions -- the run itself keeps
// going to completion on that fixed PID.
class AiAdvisorWidget : public QWidget {
  Q_OBJECT
public:
  explicit AiAdvisorWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~AiAdvisorWidget() override;

private:
  void initGui();
  void loadModels();
  void updateRunButtonEnabled();
  void runExperiment();
  void stopExperiment(bool aborted, QString const &reason = QString());
  void onDecisionTimerTick();
  void onProgressTick();
  void onShadowPidTick();
  void onTelemetryUpdated();
  void onAppStateChanged(StatusManager::AppState state);
  void onGateDisconnected();
  void onAlarmReceived(quint32 alarmCode);
  bool writePidToActiveBand(double kp, double ki, double kd);
  std::array<double, 8> buildObservation(int decisionIdx) const;
  void captureManualPid();
  bool lookupManualPidForActiveBand(double &kp, double &ki, double &kd) const;
  void revertToManualPid(QString const &reason);
  void checkOvershootAndOscillation(ai::TrajectoryPoint const &point);
  void onTwinCheckTick();
  QVector<float> buildLiveFeatureRow(double temp, double tempRef, double previousTemp, double dtS, double u,
                                      double uP, double uI, double uD, double kp, double ki, double kd) const;
  void promptForComparison();
  void runComparison();
  void onComparisonProgress(int done, int total);
  void onComparisonFinished();
  void setStatusText(QString const &text, bool isError);
  void redrawActualChart();
  void redrawComparisonChart(ai::ComparisonResult const &result);
  void setMetricsColumn(int column, ai::SimMetrics const &metrics);
  void clearMetricsTable();
  ai::SimMetrics computeActualMetrics() const;
  QWidget *buildFormCard();
  QWidget *buildChartCard();
  QWidget *buildMetricsCard();

  ResHolder       *m_resHolder;
  GruOnnxModel      m_model;
  PpoAdvisorModel   m_advisor;

  LineEditNum *m_durationEdit{nullptr};
  LineEditNum *m_dtEdit{nullptr};

  QPushButton  *m_runBtn{nullptr};
  QPushButton  *m_stopBtn{nullptr};
  QLabel       *m_statusLabel{nullptr};
  QLabel       *m_pidLabel{nullptr};
  QProgressBar *m_progressBar{nullptr};

  QCustomPlot  *m_plot{nullptr};
  QTableWidget *m_metricsTable{nullptr};

  bool          m_running{false};
  QTimer        m_decisionTimer;  // real wall-clock, fires every mpc_hold_s
  QTimer        m_progressTimer;  // ~1s UI refresh / duration-elapsed check
  QTimer        m_shadowPidTimer; // real wall-clock, fires every pid_period_s
  QTimer        m_twinCheckTimer; // real wall-clock, fires every dt_s -- live prediction-drift check
  QElapsedTimer m_runClock;

  // Safety interlock: reverts to the manual PID for the active band and
  // stops trusting the advisor for the rest of the run on the first
  // trigger. See the class comment above.
  bool         m_safetyTripped{false};
  QString      m_safetyReason;
  QVariantList m_manualPList; // pre-experiment plc_settings.pids[0].p/i/d,
  QVariantList m_manualIList; // captured once in captureManualPid() before
  QVariantList m_manualDList; // the advisor writes anything.

  int m_setpointCrossings{0};
  int m_lastErrorSign{0}; // sign of the last error outside the deadband, 0 = none yet

  QVector<float> m_liveFeatureWindow; // rolling GRU input window built from live telemetry
  bool           m_liveWindowReady{false};
  double         m_liveWindowPreviousTemp{0.0};

  bool            m_hasPendingPrediction{false};
  double          m_pendingPredictedTemp{0.0};
  QVector<double> m_recentPredictionErrors; // rolling one-step |predicted - actual|

  // Parallel shadow simulation of the real CODESYS ChamberPID/CodesysDiff
  // blocks, stepped forward with the chamber's own real measured
  // temperature/gains -- gives the trained policy's iPart/diffFilterOut
  // observation features exactly, instead of reconstructing them
  // algebraically from the scaled control outputs the chamber reports.
  ai::ChamberPid  m_shadowPid;
  ai::CodesysDiff m_shadowDiff;
  double        m_targetDurationS{0.0};
  double        m_targetTemp{0.0}; // live temp_ref captured at run start
  double        m_startTemp{0.0};  // live temp captured at run start
  double        m_tempAtLastDecision{0.0};
  double        m_realSecondsAtLastDecision{0.0};
  int           m_decisionIdx{1};
  int           m_totalDecisions{1};

  ai::SimParams                 m_currentParams; // for the later simulated comparison only
  QVector<ai::AdvisorDecision>  m_decisions;
  QVector<ai::TrajectoryPoint>  m_actualTrajectory;

  ai::ComparisonWorker *m_comparisonWorker{nullptr};
};
} // namespace control2

#endif // AIADVISORWIDGET_H
