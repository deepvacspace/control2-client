#ifndef RUNSTORE_H
#define RUNSTORE_H

#include <QByteArray>
#include <QJsonObject>
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
  };

  static QVector<StoredRun>       list(QString *error = nullptr);
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
};

} // namespace deepvaccontrol::storage

#endif // RUNSTORE_H
