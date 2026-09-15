#ifndef OFFLINEADVISORWIDGET_H
#define OFFLINEADVISORWIDGET_H

#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <QWidget>

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
// whenever the run ends, is stopped, or aborts.
class OfflineAdvisorWidget : public QWidget {
  Q_OBJECT
public:
  explicit OfflineAdvisorWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~OfflineAdvisorWidget() override;

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
  bool writePidToActiveBand(double kp, double ki, double kd);
  bool startChamberJob(double targetTemp);
  void stopChamberJob();
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

  double m_targetDurationS{0.0};
  double m_targetTemp{0.0}; // user-entered setpoint (temp_ref reads NaN while the chamber is idle)
  double m_startTemp{0.0};  // live temp captured at run start

  ai::SimParams                m_currentParams;
  QVector<ai::TrajectoryPoint> m_actualTrajectory;

  ai::OfflineTuneWorker *m_tuneWorker{nullptr};
  QProgressDialog       *m_tuneDialog{nullptr};
  ai::OfflineTuneResult  m_tuneResult;
};
} // namespace deepvaccontrol

#endif // OFFLINEADVISORWIDGET_H
