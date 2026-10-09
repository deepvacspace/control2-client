#ifndef OFFLINEADVISORWIDGET_H
#define OFFLINEADVISORWIDGET_H

#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "advisorsafety.h"
#include "chambertwinsim.h"
#include "gruonnxmodel.h"
#include "offlinetuneworker.h"
#include "resholder.h"
#include "statusmanager.h"

class QCustomPlot;
class QProgressBar;
class QProgressDialog;
class QPushButton;
class QTableWidget;

class QDialog;

namespace deepvaccontrol {
class LineEditNum;

// "Offline advisor" tab of the AI page, backed by the GRU checkpoint at
// data/model/gru_rollout.onnx. Running an experiment first searches the
// digital twin for the best fixed PID triplet for the scenario (CEM
// search, on a background thread behind a modal progress dialog), along
// with a median-cost candidate as a typical-run baseline. The user is
// then asked whether to apply the suggested PID; if accepted, it's
// written to the active gain-scheduled band, the chamber is started (a
// single-interval SETPOINT job targeting the entered temperature, the
// same JOB message Manual/Program control's Start button sends), and the
// real run begins, holding that PID fixed for the whole duration. The
// chamber is stopped (the same "stop" command their Stop button sends)
// whenever the run ends, is stopped, or aborts (disconnection, alarm, lost
// telemetry, app closed), and the operator's pre-run PID table is put back
// by ai::ManualPidGuard -- right away, or once the chamber is reachable
// again. Running requires ai::ADVISOR_CONTROL_LEVEL (losing it mid-run
// reverts to the manual PID) and a scenario inside the twin's validated
// range (GruOnnxModel::envelope()).
class OfflineAdvisorWidget : public QWidget {
  Q_OBJECT
public:
  explicit OfflineAdvisorWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~OfflineAdvisorWidget() override;

protected:
  void showEvent(QShowEvent *event) override;

private:
  void initGui();
  void loadModel();
  void updateRunButtonEnabled();
  void runExperiment();
  void startTuning();
  void onTuneProgress(int done, int total);
  void onTuneFinished();
  void startRealRun();
  void stopExperiment(bool aborted, QString const &reason = QString());
  void onProgressTick();
  void onTelemetryUpdated();
  void onAppStateChanged(StatusManager::AppState state);
  void onGateDisconnected();
  void onAlarmReceived(quint32 alarmCode);
  void onAccessChanged();
  bool writePidToActiveBand(double kp, double ki, double kd);
  bool startChamberJob(double targetTemp);
  void setStatusText(QString const &text, bool isError);
  void redrawPretunedChart();
  void redrawActualChart();
  void setMetricsColumn(int column, ai::SimMetrics const &metrics);
  void clearMetricsTable();
  ai::SimMetrics computeActualMetrics() const;
  QWidget *buildFormCard();
  QWidget *buildChartCard();
  QWidget *buildMetricsCard();

  ResHolder   *m_resHolder;
  GruOnnxModel m_model; // gru_rollout.onnx

  LineEditNum *m_durationEdit{nullptr};
  LineEditNum *m_targetTempEdit{nullptr};
  LineEditNum *m_startTempEdit{nullptr}; // chamber temperature when the tab is first shown (0 offline), then the operator's
  bool         m_startTempFilled{false}; // filled from the chamber once, when the tab is first shown
  bool         m_predictionOnly{false};  // chamber not connected: calculate and predict, send no job
  QDialog     *m_resultsDialog{nullptr};
  void         updateStartTemp();

  QPushButton  *m_runBtn{nullptr};
  QPushButton  *m_stopBtn{nullptr};
  QLabel       *m_statusLabel{nullptr};
  QLabel       *m_pidLabel{nullptr};
  QProgressBar *m_progressBar{nullptr};

  QCustomPlot  *m_plot{nullptr};
  QTableWidget *m_metricsTable{nullptr};

  bool          m_running{false};
  QTimer        m_progressTimer; // ~1s UI refresh / duration-elapsed check
  QElapsedTimer m_runClock;
  qint64        m_lastTelemetryMs{0}; // m_runClock time of the last telemetry update

  ai::ManualPidGuard *m_pidGuard{nullptr}; // pre-run PID table, put back when the run ends
  bool                m_manualRestored{false}; // reverted to manual PID mid-run
  QString             m_manualReason;

  double m_targetDurationS{0.0};
  double m_targetTemp{0.0}; // user-entered setpoint (temp_ref reads NaN while the chamber is idle)
  double m_startTemp{0.0};  // live temp captured at run start

  ai::SimParams                m_currentParams;
  QVector<ai::TrajectoryPoint> m_actualTrajectory;
  QString                      m_runId;

  ai::OfflineTuneWorker *m_tuneWorker{nullptr};
  QProgressDialog       *m_tuneDialog{nullptr};
  ai::OfflineTuneResult  m_tuneResult;
};
} // namespace deepvaccontrol

#endif // OFFLINEADVISORWIDGET_H
