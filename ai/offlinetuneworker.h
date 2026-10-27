#ifndef OFFLINETUNEWORKER_H
#define OFFLINETUNEWORKER_H

#include <QString>
#include <QThread>
#include <QVector>
#include <atomic>

#include "chambertwinsim.h"
#include "gruonnxmodel.h"

namespace deepvaccontrol::ai {

// Result of pre-run CEM tuning: the best fixed PID triplet found for the
// experiment about to run (with its twin-predicted trajectory), and the
// median-cost candidate from the same search population as a "typical
// fixed-PID run" baseline for the post-run comparison table.
struct OfflineTuneResult {
  bool    ok{false};
  bool    cancelled{false}; // stopped with cancel(): no result
  QString errorMessage;

  double                   bestKp{0.0};
  double                   bestKi{0.0};
  double                   bestKd{0.0};
  QVector<TrajectoryPoint> bestTrajectory;
  SimMetrics               bestMetrics;

  double                   medianKp{0.0};
  double                   medianKi{0.0};
  double                   medianKd{0.0};
  QVector<TrajectoryPoint> medianTrajectory;
  SimMetrics               medianMetrics;
};

// Runs one CEM search over the digital twin before the real experiment
// starts. Blocking -- run from a worker thread while a modal progress
// dialog is shown.
class OfflineTuneWorker : public QThread {
  Q_OBJECT
public:
  OfflineTuneWorker(GruOnnxModel *model, SimParams const &params, QObject *parent = nullptr);

  OfflineTuneResult const &result() const;
  // stops the search (any thread); the worker then finishes with result().cancelled
  void cancel();

signals:
  void progress(int done, int total);

protected:
  void run() override;

private:
  GruOnnxModel     *m_model;
  SimParams         m_params;
  OfflineTuneResult m_result;
  std::atomic<bool> m_cancel{false};
};

}

#endif // OFFLINETUNEWORKER_H
