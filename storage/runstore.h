#ifndef RUNSTORE_H
#define RUNSTORE_H

#include <QByteArray>
#include <QJsonObject>

#include "runmanifest.h"
#include <QStringList>
#include <QVector>
#include <optional>

namespace deepvaccontrol::storage {

// One row of Deepvac Insight's run catalog (deepvac_runs.sqlite3, table "runs").
struct StoredRun {
  QString key;
  QString id; // display name
  QString group;
  QString source;
  QString chamber;
  QString testProfile;
  QString startTime; // "yyyy-MM-dd HH:mm:ss UTC" like Insight
  QString endTime;
  qint64  samples{0};
  double  durationS{qQNaN()};
  double  mae{qQNaN()};
  double  cost{qQNaN()};
  double  tailMae{qQNaN()};
  double  overshoot{qQNaN()};
  double  settleTimeS{qQNaN()};
  // control-app extensions (extra nullable columns, ignored by Insight)
  QString runUid;          // stable id across computers
  double  startEpoch{qQNaN()};
  double  endEpoch{qQNaN()};
  core::RunManifest manifest; // self-describing part (control-app extension column manifest_json); invalid when absent
};

// When and what an experiment is, without its samples (calendar of the Chart page).
struct ExperimentSpan {
  QString key;
  QString name;
  QString source;
  double  start{qQNaN()}; // epoch s
  double  end{qQNaN()};
  qint64  samples{0};
};

// Samples of a job still running (or interrupted), journaled while it runs.
struct JournalJob {
  QString                  id;
  double                   started{qQNaN()};
  QStringList              columns;
  QJsonObject              info; // job label, chamber, manifest captured at the start
  QVector<double>          times;
  QVector<QVector<double>> values; // [column][sample]
};

struct RunSamples {
  QStringList              columns; // numeric sample columns, without "timestamp"
  QVector<double>          times;   // epoch sec
  QVector<QVector<double>> values;  // [column][sample]
};

/**
 * @brief Experiment database in Deepvac Insight's format: same file, schema and
 * samples_json layout as Insight's saved Live Monitoring sessions, so both apps
 * read each other's experiments.
 */
class RunStore {
public:
  struct NewRun {
    QString                  name;
    QString                  chamber;
    QString                  testProfile;
    QString                  source{"monitoring"};
    QString                  group{"control"};
    QString                  runUid; // generated when empty
    QStringList              columns;
    QVector<double>          times;
    QVector<QVector<double>> values;
    // identity, metadata, configuration snapshot, channels and live events captured while the job ran;
    // save() adds the events visible in the channels and the integrity part (created when empty)
    core::RunManifest manifest;
  };

  enum class Integrity : uint8_t {
    VERIFIED, // the samples match the hash recorded when the run was saved
    MODIFIED, // they do not: changed or damaged after saving
    UNKNOWN   // no hash recorded (runs from Insight / older versions)
  };

  static QVector<StoredRun>       list(QString *error = nullptr);
  // every experiment with its time span, oldest first (no samples, no manifest: fast)
  static QVector<ExperimentSpan>  spans(QString *error = nullptr);

  // ---- journal of the running job: samples are appended while it runs, so an interrupted job
  // (crash, power loss) can still be saved as an experiment at the next start
  static bool                journalBegin(QString const &id, double started, QStringList const &columns, QJsonObject const &info);
  // rows: [sample][column]
  static bool                journalAppend(QString const &id, QVector<double> const &times, QVector<QVector<double>> const &rows);
  static void                journalEnd(QString const &id);
  static QVector<JournalJob> journalPending();
  static std::optional<StoredRun> get(QString const &key);
  static std::optional<StoredRun> findByUid(QString const &runUid);
  // a stored run covering [start, end] of the same chamber, to avoid saving it twice
  static std::optional<StoredRun> findCovering(QString const &chamber, double start, double end);

  // returns the new run's key, empty on error
  static QString save(NewRun const &run, QString *error = nullptr);
  static bool    loadSamples(QString const &key, RunSamples &out, QString *error = nullptr);
  static bool    rename(QString const &key, QString const &name);
  static bool    remove(QString const &key);

  // collaboration: metadata as exchanged by Insight's "run_metadata" kind, plus raw samples_json
  static QJsonObject metadataJson(StoredRun const &run);
  static QByteArray  samplesJson(QString const &key);
  static QString     importShared(QJsonObject const &metadata, QByteArray const &samplesJson, QString *error = nullptr);

  static QString makeRunUid(QString const &chamberRef, double start, double end);

  // KPIs into mae, cost, tail_mae, overshoot, settle_time_s and the structured analysis
  // (core::analyseExperiment: stages with details, steady states, saturation, P/I/D, anomalies,
  // configuration) into summary_json; returns false when the run has no temp / temp_ref
  static bool recomputeKpis(QString const &key);
  // computes them for stored runs analysed by an older version (or never); returns how many were updated
  static int backfillKpis();
  static QString formatTime(double epochSec);

  // ---- self-describing runs
  static std::optional<core::RunManifest> manifest(QString const &key);
  static bool                             updateMetadata(QString const &key, core::RunMetadata const &metadata);
  static Integrity                        verify(QString const &key);
  static Integrity                        verify(core::RunManifest const &manifest, QByteArray const &samplesJson);
  // the whole run as one JSON document: identity, metadata, configuration, channels, samples, analysis
  // (stages, KPIs, anomalies), events, annotations and integrity, for archiving or exchange
  static QJsonObject                      bundle(QString const &key, QString *error = nullptr);
};

}

#endif // RUNSTORE_H
