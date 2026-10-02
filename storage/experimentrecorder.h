#ifndef EXPERIMENTRECORDER_H
#define EXPERIMENTRECORDER_H

#include <QObject>

#include "hardwarestatus.h"
#include "runstore.h"

namespace deepvaccontrol {

struct ResHolder;

/**
 * @brief Saves every finished chamber job (manual setpoint, program, test profile) to the
 * experiment database (storage::RunStore, Deepvac Insight's format) and helps saving any
 * recorded time range on demand.
 */
class ExperimentRecorder : public QObject {
  Q_OBJECT
public:
  static constexpr double MIN_RUN_DURATION_SEC{60.0};

  explicit ExperimentRecorder(ResHolder *resHolder, QObject *parent = nullptr);

  // describes the job about to be started (shown in the run name / test_profile column)
  void setNextJob(QString const &label, QString const &testProfile = {});

  [[nodiscard]] QString chamberName() const;
  [[nodiscard]] QString chamberRef() const; // "host:port" of the PLC gate, identifies the chamber across computers

  // recorded samples of [start, end] from the interval files and the live, not yet saved data
  bool collect(double start, double end, storage::RunStore::NewRun &out, QString *error = nullptr) const;
  // saves in a background thread; emits runSaved / saveFailed
  void saveAsync(storage::RunStore::NewRun run);

signals:
  void runSaved(QString const &key, QString const &name);
  void saveFailed(QString const &error);

private:
  void onStatusChanged(HardwareStatus::HardwareStatusEnum newStatus, HardwareStatus::HardwareStatusEnum oldStatus);
  void finishJob(bool alarm);

  ResHolder *m_resHolder;
  double     m_jobStart{qQNaN()};
  QString    m_jobLabel;
  QString    m_jobTestProfile;
  QString    m_nextLabel;
  QString    m_nextTestProfile;
};

} // namespace deepvaccontrol

#endif // EXPERIMENTRECORDER_H
