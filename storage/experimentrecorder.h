#ifndef EXPERIMENTRECORDER_H
#define EXPERIMENTRECORDER_H

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include "hardwarestatus.h"
#include "runmanifest.h"
#include "runstore.h"

namespace deepvaccontrol {

struct ResHolder;

/**
 * @brief Saves every finished chamber job (manual setpoint, program, test profile, AI runs) to the
 * experiment database (storage::RunStore, Deepvac Insight's format) as a self-describing run, and
 * helps saving any recorded time range on demand.
 *
 * Captured while the experiment runs, not reconstructed afterwards:
 * - the configuration snapshot (program / profile sent, PID table, calibration, control parameters,
 *   alarms, application version, user, chamber) when the job is sent;
 * - live events: job start / end, operator and PLC actions, PLC alarms, user changes, PID table writes;
 * - the run details (DUT, operator, purpose, tags) entered for the next run.
 * Events visible in the recorded channels (setpoint, gains, PID band, status, program position,
 * alarm bits) are added from the samples when the run is saved (RunStore::save).
 */
class ExperimentRecorder : public QObject {
  Q_OBJECT
public:
  static constexpr double MIN_RUN_DURATION_SEC{60.0};

  // what is about to be started
  struct JobInfo {
    QString     kind;        // "manual_setpoint", "program", "test_profile", "ai_advisor", "offline_advisor"
    QString     label;       // run name suffix
    QString     testProfile; // profile / library program name
    QVariantMap program;     // ProgramModel::programVariant() as sent to the PLC
    QJsonObject details;     // kind specific (AI target, duration, ...)
  };

  explicit ExperimentRecorder(ResHolder *resHolder, QObject *parent = nullptr);

  // the job about to be started: takes the configuration snapshot now
  void setNextJob(JobInfo const &job);
  void setNextJob(QString const &label, QString const &testProfile = {});

  // run details for the next runs (kept until changed); the operator defaults to the logged-in user
  void                            setNextRunMetadata(core::RunMetadata const &metadata);
  [[nodiscard]] core::RunMetadata nextRunMetadata() const;

  [[nodiscard]] QString chamberName() const;
  // the running job sets the pressure and not the temperature (from the program it was sent with)
  [[nodiscard]] bool pressureControlled() const;
  [[nodiscard]] QString chamberRef() const; // "host:port" of the PLC gate, identifies the chamber across computers
  // the job running now: its kind ("manual_setpoint", "program", ...), program (ProgramModel::programVariant())
  // and start (epoch s); empty / NaN when none runs
  [[nodiscard]] QString     runningJobKind() const;
  [[nodiscard]] QVariantMap runningProgram() const;
  [[nodiscard]] double      runningJobStart() const;

  // every sample received from the PLC (App); kept while a job runs and journaled to the experiment database
  void addSample(double epochSec, QVector<double> const &values);
  // saves in a background thread; emits runSaved / saveFailed. journalId: journal of the job, removed once saved
  void saveAsync(storage::RunStore::NewRun run, QString const &journalId = {});

  // configuration of the chamber and the application right now, as stored in a run's manifest
  [[nodiscard]] QJsonObject configurationSnapshot(JobInfo const &job) const;

signals:
  void runSaved(QString const &key, QString const &name);
  void saveFailed(QString const &error);
  void nextRunMetadataChanged();

private:
  struct Job {
    JobInfo                 info;
    QJsonObject             configuration;
    core::RunMetadata       metadata;
    QVector<core::RunEvent> events;
    double                  start{qQNaN()};
    // samples of the job, recorded directly (no interval files)
    QString                  journalId;
    QStringList              columns;
    QVector<double>          times;
    QVector<QVector<double>> values;      // [column][sample]
    QVector<double>          newTimes;    // not yet journaled
    QVector<QVector<double>> newRows;     // [sample][column]
  };

  void onStatusChanged(HardwareStatus::HardwareStatusEnum newStatus, HardwareStatus::HardwareStatusEnum oldStatus);
  void beginJob(QString const &note);
  void finishJob(HardwareStatus::HardwareStatusEnum endStatus);
  void addEvent(char const *type, QString const &message, QJsonObject const &data = {});
  void onPlcSettingsChanged();
  [[nodiscard]] bool jobActive() const;
  void               flushJournal();
  // jobs whose application stopped while they ran: saved from their journal as "(interrupted)"
  void               recoverInterrupted();

  ResHolder        *m_resHolder;
  Job               m_job;
  bool              m_hasPending{false};
  JobInfo           m_pendingJob;
  QJsonObject       m_pendingConfiguration;
  double            m_pendingAt{qQNaN()};
  core::RunMetadata m_nextMetadata;
  QJsonValue        m_lastPids; // PID table last seen, to log writes during a job
  QTimer           *m_journalTimer{nullptr};
};

}

#endif // EXPERIMENTRECORDER_H
