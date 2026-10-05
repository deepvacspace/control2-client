#include "runstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimeZone>
#include <algorithm>
#include <cmath>

#include "annotationsdb.h"
#include "experimentanalysis.h"
#include "sqlitedb.h"

namespace deepvaccontrol::storage {

namespace {
constexpr int    CACHE_VERSION{1}; // Insight's data_service.CACHE_VERSION
constexpr int    NUMERIC_PROBE_ROWS{50};
QString const    SELECT_COLUMNS{"key, id, group_name, source, chamber, test_profile, start_time, end_time, samples, "
                                "duration_s, mae, cost, tail_mae, overshoot, settle_time_s, run_uid, start_epoch, end_epoch, manifest_json"};
// Insight's numeric_columns() never treats these as channels
QSet<QString> const NON_CHANNEL_COLUMNS{"run_id", "timestamp", "start_temp"};

QVariant nullable(double value) {
  return std::isnan(value) ? QVariant() : QVariant(value);
}

QVariant nullable(QString const &value) {
  return value.isEmpty() ? QVariant() : QVariant(value);
}

double toDouble(QVariant const &value) {
  return value.isNull() ? qQNaN() : value.toDouble();
}

bool ensureSchema(ScopedDb &db) {
  if (!db.isOpen()) {
    return false;
  }
  db.exec("PRAGMA synchronous=NORMAL");
  db.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)");
  // identical to Insight's data_service.connect_cache()
  db.exec(R"(CREATE TABLE IF NOT EXISTS runs (
            key TEXT PRIMARY KEY,
            id TEXT NOT NULL,
            group_name TEXT NOT NULL,
            root_path TEXT NOT NULL,
            run_path TEXT NOT NULL,
            samples_path TEXT NOT NULL,
            source_mtime REAL NOT NULL,
            samples INTEGER NOT NULL,
            duration_s REAL,
            mae REAL,
            cost REAL,
            tail_mae REAL,
            overshoot REAL,
            settle_time_s REAL,
            start_time TEXT,
            end_time TEXT,
            columns_json TEXT NOT NULL,
            numeric_columns_json TEXT NOT NULL,
            summary_json TEXT NOT NULL,
            bands_json TEXT NOT NULL,
            annotations_json TEXT NOT NULL,
            samples_json TEXT NOT NULL,
            cached_at TEXT NOT NULL
        ))");
  db.exec("CREATE INDEX IF NOT EXISTS idx_runs_cached_at ON runs(cached_at)");
  db.exec("CREATE INDEX IF NOT EXISTS idx_runs_id ON runs(id)");

  QStringList const existing = db.columns("runs");
  if (!existing.contains("source")) {
    db.exec("ALTER TABLE runs ADD COLUMN source TEXT NOT NULL DEFAULT 'folder'");
  }
  if (!existing.contains("chamber")) {
    db.exec("ALTER TABLE runs ADD COLUMN chamber TEXT");
  }
  if (!existing.contains("test_profile")) {
    db.exec("ALTER TABLE runs ADD COLUMN test_profile TEXT");
  }
  // control-app extensions
  if (!existing.contains("run_uid")) {
    db.exec("ALTER TABLE runs ADD COLUMN run_uid TEXT");
  }
  if (!existing.contains("start_epoch")) {
    db.exec("ALTER TABLE runs ADD COLUMN start_epoch REAL");
  }
  if (!existing.contains("end_epoch")) {
    db.exec("ALTER TABLE runs ADD COLUMN end_epoch REAL");
  }
  if (!existing.contains("manifest_json")) {
    db.exec("ALTER TABLE runs ADD COLUMN manifest_json TEXT"); // core::RunManifest, ignored by Insight
  }
  db.exec("CREATE INDEX IF NOT EXISTS idx_runs_run_uid ON runs(run_uid)");
  db.exec("INSERT OR REPLACE INTO meta(key, value) VALUES('cache_version', ?)", {QString::number(CACHE_VERSION)});
  return true;
}

StoredRun rowToRun(QSqlQuery const &q) {
  StoredRun run;
  run.key         = q.value("key").toString();
  run.id          = q.value("id").toString();
  run.group       = q.value("group_name").toString();
  run.source      = q.value("source").toString();
  run.chamber     = q.value("chamber").toString();
  run.testProfile = q.value("test_profile").toString();
  run.startTime   = q.value("start_time").toString();
  run.endTime     = q.value("end_time").toString();
  run.samples     = q.value("samples").toLongLong();
  run.durationS   = toDouble(q.value("duration_s"));
  run.mae         = toDouble(q.value("mae"));
  run.cost        = toDouble(q.value("cost"));
  run.tailMae     = toDouble(q.value("tail_mae"));
  run.overshoot   = toDouble(q.value("overshoot"));
  run.settleTimeS = toDouble(q.value("settle_time_s"));
  run.runUid      = q.value("run_uid").toString();
  run.startEpoch  = toDouble(q.value("start_epoch"));
  run.endEpoch    = toDouble(q.value("end_epoch"));
  if (QString const manifest = q.value("manifest_json").toString(); !manifest.isEmpty()) {
    run.manifest = core::RunManifest::fromJson(QJsonDocument::fromJson(manifest.toUtf8()).object());
  }
  return run;
}

// The manifest of a run being saved: what was captured during the job, plus the events visible in the
// recorded channels and the integrity part. A run saved without capture (time range saved by hand,
// imported, older) gets the parts that can be derived: identity, channels by name, signal events.
core::RunManifest completeManifest(core::RunManifest manifest, QString const &name, QString const &chamber, QString const &runUid,
                                   QStringList const &columns, QVector<double> const &times, QVector<QVector<double>> const &values,
                                   QByteArray const &samplesJson) {
  if (manifest.runUid.isEmpty()) {
    manifest.runUid = runUid;
  }
  if (manifest.name.isEmpty()) {
    manifest.name = name;
  }
  if (manifest.chamber.isEmpty()) {
    manifest.chamber = chamber;
  }
  manifest.start = times.first();
  manifest.end   = times.last();
  if (manifest.channels.isEmpty()) {
    QVariantList states;
    for (QString const &column : columns) {
      states.push_back(QVariantMap{{"name", column}});
    }
    manifest.channels = core::channelMetadata(states, columns);
  }
  if (manifest.configuration.isEmpty()) {
    manifest.configuration = QJsonObject{{"captured_at_execution", false}};
  }
  // events visible in the channels, at their sample times (added once)
  bool const haveSignalEvents = std::any_of(manifest.events.cbegin(), manifest.events.cend(),
                                            [](core::RunEvent const &event) { return event.source == QLatin1String("signal"); });
  if (!haveSignalEvents) {
    QHash<QString, QVector<double>> channels;
    for (qsizetype c{0}; (c < columns.size()) && (c < values.size()); ++c) {
      channels.insert(columns[c], values[c]);
    }
    manifest.events += core::detectEvents(times, channels, core::alarmLabels(manifest.configuration.value("alarms").toArray().toVariantList()));
    core::sortEvents(manifest.events);
  }
  bool const fractional = std::any_of(times.cbegin(), times.cend(), [](double t) { return t != std::floor(t); });
  manifest.timeResolutionS = fractional ? 0.001 : 1.0;
  manifest.samplesSha256   = core::sha256Hex(samplesJson);
  manifest.sampleCount     = times.size();
  manifest.analysisVersion = core::EXPERIMENT_ANALYSIS_VERSION;
  return manifest;
}

QString manifestText(core::RunManifest const &manifest) {
  return QString::fromUtf8(QJsonDocument(manifest.toJson()).toJson(QJsonDocument::Compact));
}

// Insight's _sanitize_key_component()
QString sanitizeKeyComponent(QString const &text) {
  QString safe;
  for (QChar const ch : text) {
    safe += (ch.isLetterOrNumber() || ch == '-' || ch == '_') ? ch : QChar('_');
  }
  while (safe.startsWith('_')) {
    safe.remove(0, 1);
  }
  while (safe.endsWith('_')) {
    safe.chop(1);
  }
  return safe.isEmpty() ? QStringLiteral("run") : safe;
}

// Insight's _unique_key()
QString uniqueKey(ScopedDb &db, QString const &prefix, QString const &name) {
  QString const base = prefix + "/" + sanitizeKeyComponent(name);
  QString       key  = base;
  for (int suffix{2};; ++suffix) {
    QSqlQuery q = db.query("SELECT 1 FROM runs WHERE key = ?", {key});
    if (!q.next()) {
      return key;
    }
    key = QStringLiteral("%1_%2").arg(base).arg(suffix);
  }
}

QByteArray jsonNumber(double value) {
  return std::isnan(value) || std::isinf(value) ? QByteArrayLiteral("null") : QByteArray::number(value, 'g', 12);
}

QByteArray jsonString(QString const &text) {
  // QJsonDocument escapes correctly; strip the surrounding array brackets
  QByteArray const array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
  return array.mid(1, array.size() - 2);
}

// KPIs and structured analysis of a run with temp / temp_ref; summary is the JSON stored in summary_json
bool analyse(QStringList const &columns, QVector<double> const &times, QVector<QVector<double>> const &values, StoredRun &run,
             QString &summary) {
  auto column = [&](QString const &name) {
    qsizetype const index{columns.indexOf(name)};
    return (index >= 0) && (index < values.size()) ? values[index] : QVector<double>{};
  };
  QVector<double> const temp      = column("temp");
  QVector<double> const reference = column("temp_ref");
  if (temp.isEmpty() || reference.isEmpty() || times.isEmpty()) {
    return false;
  }
  QVector<double> elapsed;
  elapsed.reserve(times.size());
  for (double const t : times) {
    elapsed.push_back(t - times.first());
  }
  core::ExperimentInput input;
  input.times = elapsed;
  for (qsizetype c{0}; (c < columns.size()) && (c < values.size()); ++c) {
    input.channels.insert(columns[c], values[c]);
  }
  core::ExperimentAnalysis const analysis = core::analyseExperiment(input);
  if (!analysis.hasControl) {
    return false;
  }
  run.mae         = analysis.run.overall.mae;
  run.cost        = analysis.run.cost;
  run.tailMae     = analysis.run.overall.tailMae;
  run.overshoot   = analysis.run.overshoot;
  run.settleTimeS = analysis.run.settlingTimeS;
  summary         = QString::fromUtf8(QJsonDocument(analysis.toJson()).toJson(QJsonDocument::Compact));
  return true;
}

QString insertRun(ScopedDb &db, StoredRun const &run, QStringList const &columns, QStringList const &numeric,
                  QByteArray const &samplesJson, QString *error, QString const &summaryJson = QStringLiteral("{}")) {
  QSqlQuery q = db.query(
    R"(INSERT INTO runs (
         key, id, group_name, root_path, run_path, samples_path, source_mtime,
         samples, duration_s, mae, cost, tail_mae, overshoot, settle_time_s,
         start_time, end_time, columns_json, numeric_columns_json,
         summary_json, bands_json, annotations_json, samples_json, source,
         chamber, test_profile, cached_at, run_uid, start_epoch, end_epoch, manifest_json
       ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'), ?, ?, ?, ?))",
    {run.key,
     run.id,
     run.group,
     QStringLiteral("control-app"),
     QStringLiteral("control-app"),
     QStringLiteral("control-app"),
     0.0,
     run.samples,
     nullable(run.durationS),
     nullable(run.mae),
     nullable(run.cost),
     nullable(run.tailMae),
     nullable(run.overshoot),
     nullable(run.settleTimeS),
     run.startTime,
     run.endTime,
     QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(columns)).toJson(QJsonDocument::Compact)),
     QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(numeric)).toJson(QJsonDocument::Compact)),
     summaryJson,
     QStringLiteral("[]"),
     QStringLiteral("[]"),
     QString::fromUtf8(samplesJson),
     run.source,
     nullable(run.chamber),
     nullable(run.testProfile),
     run.runUid,
     nullable(run.startEpoch),
     nullable(run.endEpoch),
     run.manifest.isValid() ? QVariant(manifestText(run.manifest)) : QVariant()});
  if (q.lastError().isValid()) {
    if (error) {
      *error = q.lastError().text();
    }
    return {};
  }
  return run.key;
}
} // namespace

QString RunStore::formatTime(double epochSec) {
  if (std::isnan(epochSec)) {
    return {};
  }
  return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(epochSec * 1000.0), QTimeZone::UTC)
           .toString("yyyy-MM-dd HH:mm:ss")
         + " UTC";
}

QString RunStore::makeRunUid(QString const &chamberRef, double start, double end) {
  QByteArray const seed = QStringLiteral("%1|%2|%3")
                            .arg(chamberRef.trimmed().toLower())
                            .arg(static_cast<qint64>(start))
                            .arg(static_cast<qint64>(end))
                            .toUtf8();
  return QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().left(32));
}

QVector<StoredRun> RunStore::list(QString *error) {
  ScopedDb db(RUNS_DB);
  if (!ensureSchema(db)) {
    if (error) {
      *error = db.lastError();
    }
    return {};
  }
  QVector<StoredRun> result;
  QSqlQuery          q = db.query(QStringLiteral("SELECT %1 FROM runs ORDER BY COALESCE(start_epoch, 0) DESC, start_time DESC").arg(SELECT_COLUMNS));
  while (q.next()) {
    result.push_back(rowToRun(q));
  }
  return result;
}

std::optional<StoredRun> RunStore::get(QString const &key) {
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query(QStringLiteral("SELECT %1 FROM runs WHERE key = ?").arg(SELECT_COLUMNS), {key});
  if (q.next()) {
    return rowToRun(q);
  }
  return std::nullopt;
}

std::optional<StoredRun> RunStore::findByUid(QString const &runUid) {
  if (runUid.isEmpty()) {
    return std::nullopt;
  }
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query(QStringLiteral("SELECT %1 FROM runs WHERE run_uid = ?").arg(SELECT_COLUMNS), {runUid});
  if (q.next()) {
    return rowToRun(q);
  }
  return std::nullopt;
}

std::optional<StoredRun> RunStore::findCovering(QString const &chamber, double start, double end) {
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query(QStringLiteral("SELECT %1 FROM runs WHERE COALESCE(chamber, '') = ? AND start_epoch <= ? "
                                        "AND end_epoch >= ? LIMIT 1")
                           .arg(SELECT_COLUMNS),
                         {chamber, start + 1.0, end - 1.0});
  if (q.next()) {
    return rowToRun(q);
  }
  return std::nullopt;
}

QString RunStore::save(NewRun const &input, QString *error) {
  if (input.name.trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("Run name cannot be empty.");
    }
    return {};
  }
  if (input.times.isEmpty()) {
    if (error) {
      *error = QStringLiteral("No samples were recorded in this session.");
    }
    return {};
  }

  // samples_json: one object per sample like Insight's monitoring sessions,
  // {"timestamp": <epoch s>, "<state>": value | null, ...}; kp/ki/kd aliases as sent to Insight
  QStringList columns{"timestamp"};
  columns.append(input.columns);
  QVector<QPair<QString, int>> aliases; // alias name, source column (ordered like `columns`)
  for (auto const &[state, alias] : {std::pair{"temp_kp", "kp"}, std::pair{"temp_ki", "ki"}, std::pair{"temp_kd", "kd"}}) {
    int const index = static_cast<int>(input.columns.indexOf(state));
    if ((index >= 0) && !input.columns.contains(alias)) {
      aliases.push_back({alias, index});
      columns.push_back(alias);
    }
  }

  QList<QByteArray> keys;
  for (QString const &column : std::as_const(columns)) {
    keys.push_back(jsonString(column) + ':');
  }

  QByteArray samplesJson;
  samplesJson.reserve(input.times.size() * (columns.size() * 14 + 16));
  samplesJson += '[';
  QVector<bool> seen(input.columns.size(), false);
  for (qsizetype i{0}; i < input.times.size(); ++i) {
    samplesJson += (i == 0) ? "{" : ",{";
    samplesJson += keys[0] + jsonNumber(input.times[i]);
    for (int c{0}; c < input.columns.size(); ++c) {
      double const value = (c < input.values.size()) && (i < input.values[c].size()) ? input.values[c][i] : qQNaN();
      if ((i < NUMERIC_PROBE_ROWS) && !std::isnan(value)) {
        seen[c] = true;
      }
      samplesJson += ',' + keys[c + 1] + jsonNumber(value);
    }
    int aliasPos{static_cast<int>(input.columns.size()) + 1};
    for (auto const &[alias, c] : aliases) {
      samplesJson += ',' + keys[aliasPos++] + jsonNumber(i < input.values[c].size() ? input.values[c][i] : qQNaN());
    }
    samplesJson += '}';
  }
  samplesJson += ']';

  QStringList numeric;
  for (int c{0}; c < input.columns.size(); ++c) {
    if (seen[c] && !NON_CHANNEL_COLUMNS.contains(input.columns[c])) {
      numeric.push_back(input.columns[c]);
    }
  }
  for (auto const &[alias, c] : aliases) {
    if (seen[c]) {
      numeric.push_back(alias);
    }
  }

  ScopedDb db(RUNS_DB);
  if (!ensureSchema(db)) {
    if (error) {
      *error = db.lastError();
    }
    return {};
  }

  StoredRun run;
  run.key         = uniqueKey(db, "control", input.name);
  run.id          = input.name.trimmed();
  run.group       = input.group;
  run.source      = input.source;
  run.chamber     = input.chamber;
  run.testProfile = input.testProfile;
  run.samples     = input.times.size();
  run.startEpoch  = input.times.first();
  run.endEpoch    = input.times.last();
  run.durationS   = run.endEpoch - run.startEpoch;
  run.startTime   = formatTime(run.startEpoch);
  run.endTime     = formatTime(run.endEpoch);
  run.runUid      = input.runUid.isEmpty() ? makeRunUid(input.chamber, run.startEpoch, run.endEpoch) : input.runUid;
  QString summary{QStringLiteral("{}")};
  analyse(input.columns, input.times, input.values, run, summary);
  run.manifest = completeManifest(input.manifest, run.id, run.chamber, run.runUid, input.columns, input.times, input.values, samplesJson);
  return insertRun(db, run, columns, numeric, samplesJson, error, summary);
}

bool RunStore::loadSamples(QString const &key, RunSamples &out, QString *error) {
  QByteArray const json = samplesJson(key);
  if (json.isEmpty()) {
    if (error) {
      *error = QStringLiteral("Run %1 not found").arg(key);
    }
    return false;
  }

  QJsonParseError    parseError;
  QJsonDocument const doc = QJsonDocument::fromJson(json, &parseError);
  if (!doc.isArray()) {
    if (error) {
      *error = parseError.errorString();
    }
    return false;
  }

  QJsonArray const rows = doc.array();
  out                   = RunSamples{};
  QHash<QString, int> columnIndex;
  qsizetype           rowIndex{0};
  for (QJsonValue const &rowValue : rows) {
    QJsonObject const row = rowValue.toObject();
    double const      timestamp{row.value("timestamp").isDouble() ? row.value("timestamp").toDouble() : qQNaN()};
    if (std::isnan(timestamp)) {
      continue; // Insight falls back to the row index; without a time the sample cannot be placed
    }
    out.times.push_back(timestamp);
    for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
      if (NON_CHANNEL_COLUMNS.contains(it.key()) || !(it.value().isDouble() || it.value().isNull())) {
        continue;
      }
      int index = columnIndex.value(it.key(), -1);
      if (index < 0) {
        index = static_cast<int>(out.columns.size());
        columnIndex.insert(it.key(), index);
        out.columns.push_back(it.key());
        out.values.push_back(QVector<double>(rowIndex, qQNaN()));
      }
      QVector<double> &column = out.values[index];
      column.resize(rowIndex, qQNaN());
      column.push_back(it.value().isDouble() ? it.value().toDouble() : qQNaN());
    }
    ++rowIndex;
    for (auto &column : out.values) {
      column.resize(rowIndex, qQNaN());
    }
  }
  return !out.times.isEmpty();
}

bool RunStore::recomputeKpis(QString const &key) {
  RunSamples samples;
  if (!loadSamples(key, samples)) {
    return false;
  }
  StoredRun run;
  QString   summary;
  if (!analyse(samples.columns, samples.times, samples.values, run, summary)) {
    return false;
  }
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  bool ok = db.exec("UPDATE runs SET mae = ?, cost = ?, tail_mae = ?, overshoot = ?, settle_time_s = ?, summary_json = ? WHERE key = ?",
                    {nullable(run.mae), nullable(run.cost), nullable(run.tailMae), nullable(run.overshoot), nullable(run.settleTimeS),
                     summary, key});
  // runs saved before manifests existed get the derivable part (no configuration snapshot, no live events)
  QString name, chamber, runUid;
  bool    missing{false};
  {
    QSqlQuery existing = db.query("SELECT id, chamber, run_uid, manifest_json FROM runs WHERE key = ?", {key});
    if (existing.next()) {
      name    = existing.value(0).toString();
      chamber = existing.value(1).toString();
      runUid  = existing.value(2).toString();
      missing = existing.value(3).toString().isEmpty();
    }
  } // the read must be finished before the same connection writes the row
  if (missing) {
    core::RunManifest const manifest =
      completeManifest({}, name, chamber, runUid, samples.columns, samples.times, samples.values, samplesJson(key));
    ok = db.exec("UPDATE runs SET manifest_json = ? WHERE key = ?", {manifestText(manifest), key}) && ok;
  }
  return ok;
}

int RunStore::backfillKpis() {
  QStringList keys;
  {
    ScopedDb db(RUNS_DB);
    ensureSchema(db);
    // runs without the current structured analysis (stage details, anomalies, configuration)
    QString const current = QStringLiteral("%\"analysis_version\":%1,%").arg(core::EXPERIMENT_ANALYSIS_VERSION);
    QSqlQuery     q       = db.query("SELECT key FROM runs WHERE (summary_json NOT LIKE ? AND columns_json LIKE '%\"temp_ref\"%') "
                                         "OR manifest_json IS NULL",
                                         {current});
    while (q.next()) {
      keys.push_back(q.value(0).toString());
    }
  }
  int updated{0};
  for (QString const &key : std::as_const(keys)) {
    updated += recomputeKpis(key) ? 1 : 0;
  }
  return updated;
}

QByteArray RunStore::samplesJson(QString const &key) {
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query("SELECT samples_json FROM runs WHERE key = ?", {key});
  return q.next() ? q.value(0).toString().toUtf8() : QByteArray{};
}

bool RunStore::rename(QString const &key, QString const &name) {
  if (name.trimmed().isEmpty()) {
    return false;
  }
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  return db.exec("UPDATE runs SET id = ? WHERE key = ?", {name.trimmed(), key});
}

bool RunStore::remove(QString const &key) {
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  return db.exec("DELETE FROM runs WHERE key = ?", {key});
}

QJsonObject RunStore::metadataJson(StoredRun const &run) {
  auto number = [](double value) {
    return std::isnan(value) ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
  };
  auto text = [](QString const &value) {
    return value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
  };
  // Insight's _RUN_METADATA_FIELDS, plus the control-app epoch range
  return QJsonObject{{"id", run.id},
                     {"group", text(run.group)},
                     {"samples", run.samples},
                     {"duration_s", number(run.durationS)},
                     {"mae", number(run.mae)},
                     {"cost", number(run.cost)},
                     {"tail_mae", number(run.tailMae)},
                     {"overshoot", number(run.overshoot)},
                     {"settle_time_s", number(run.settleTimeS)},
                     {"start_time", text(run.startTime)},
                     {"end_time", text(run.endTime)},
                     {"chamber", text(run.chamber)},
                     {"test_profile", text(run.testProfile)},
                     {"start_epoch", number(run.startEpoch)},
                     {"end_epoch", number(run.endEpoch)},
                     // control-app extensions: integrity and run details
                     {"sha256", text(run.manifest.samplesSha256)},
                     {"dut", text(run.manifest.metadata.dut)},
                     {"operator", text(run.manifest.metadata.operatorName)},
                     {"purpose", text(run.manifest.metadata.purpose)},
                     {"tags", QJsonArray::fromStringList(run.manifest.metadata.tags)}};
}

QString RunStore::importShared(QJsonObject const &metadata, QByteArray const &samplesJsonData, QString *error) {
  QString const runUid = metadata.value("uid").toString();
  if (auto existing = findByUid(runUid)) {
    return existing->key; // already here
  }

  QJsonParseError     parseError;
  QJsonDocument const doc = QJsonDocument::fromJson(samplesJsonData, &parseError);
  if (!doc.isArray() || doc.array().isEmpty()) {
    if (error) {
      *error = QStringLiteral("Invalid samples: %1").arg(parseError.errorString());
    }
    return {};
  }
  // the manifest recorded on the other computer travels with the samples: their hash must match
  core::RunManifest const sharedManifest = core::RunManifest::fromJson(metadata.value("manifest").toObject());
  if (sharedManifest.isValid() && (verify(sharedManifest, samplesJsonData) == Integrity::MODIFIED)) {
    if (error) {
      *error = QStringLiteral("The experiment data does not match its content hash (damaged in transfer).");
    }
    return {};
  }

  QJsonArray const rows = doc.array();
  QStringList      columns = rows.first().toObject().keys();
  QStringList      numeric;
  for (QString const &column : std::as_const(columns)) {
    if (NON_CHANNEL_COLUMNS.contains(column)) {
      continue;
    }
    for (qsizetype i{0}; i < std::min<qsizetype>(rows.size(), NUMERIC_PROBE_ROWS); ++i) {
      if (rows[i].toObject().value(column).isDouble()) {
        numeric.push_back(column);
        break;
      }
    }
  }

  auto number = [&metadata](char const *name) {
    QJsonValue const value = metadata.value(QLatin1String(name));
    return value.isDouble() ? value.toDouble() : qQNaN();
  };

  ScopedDb db(RUNS_DB);
  if (!ensureSchema(db)) {
    if (error) {
      *error = db.lastError();
    }
    return {};
  }
  StoredRun run;
  run.id          = metadata.value("id").toString(runUid);
  run.key         = uniqueKey(db, "shared", run.id);
  run.group       = QStringLiteral("shared");
  run.source      = QStringLiteral("collab");
  run.chamber     = metadata.value("chamber").toString();
  run.testProfile = metadata.value("test_profile").toString();
  run.startTime   = metadata.value("start_time").toString();
  run.endTime     = metadata.value("end_time").toString();
  run.samples     = rows.size();
  run.durationS   = number("duration_s");
  run.mae         = number("mae");
  run.cost        = number("cost");
  run.tailMae     = number("tail_mae");
  run.overshoot   = number("overshoot");
  run.settleTimeS = number("settle_time_s");
  run.startEpoch  = number("start_epoch");
  run.endEpoch    = number("end_epoch");
  run.runUid      = runUid;
  run.manifest    = sharedManifest;
  if (!run.manifest.isValid()) {
    // shared without a manifest (Insight, older version): derivable part, hashed as received
    QStringList sampleColumns = numeric;
    QVector<double> times;
    QVector<QVector<double>> values(sampleColumns.size());
    for (QJsonValue const &rowValue : rows) {
      QJsonObject const row = rowValue.toObject();
      if (!row.value("timestamp").isDouble()) {
        continue;
      }
      times.push_back(row.value("timestamp").toDouble());
      for (qsizetype c{0}; c < sampleColumns.size(); ++c) {
        QJsonValue const value = row.value(sampleColumns[c]);
        values[c].push_back(value.isDouble() ? value.toDouble() : qQNaN());
      }
    }
    if (!times.isEmpty()) {
      run.manifest = completeManifest({}, run.id, run.chamber, runUid, sampleColumns, times, values, samplesJsonData);
    }
  }
  return insertRun(db, run, columns, numeric, samplesJsonData, error);
}

std::optional<core::RunManifest> RunStore::manifest(QString const &key) {
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query("SELECT manifest_json FROM runs WHERE key = ?", {key});
  if (!q.next() || q.value(0).toString().isEmpty()) {
    return std::nullopt;
  }
  return core::RunManifest::fromJson(QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object());
}

bool RunStore::updateMetadata(QString const &key, core::RunMetadata const &metadata) {
  std::optional<core::RunManifest> current = manifest(key);
  if (!current) {
    return false;
  }
  current->metadata = metadata; // the samples are unchanged: their hash stays valid
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  return db.exec("UPDATE runs SET manifest_json = ? WHERE key = ?", {manifestText(*current), key});
}

RunStore::Integrity RunStore::verify(core::RunManifest const &manifest, QByteArray const &samples) {
  if (manifest.samplesSha256.isEmpty()) {
    return Integrity::UNKNOWN;
  }
  return core::sha256Hex(samples) == manifest.samplesSha256 ? Integrity::VERIFIED : Integrity::MODIFIED;
}

RunStore::Integrity RunStore::verify(QString const &key) {
  std::optional<core::RunManifest> const stored = manifest(key);
  return stored ? verify(*stored, samplesJson(key)) : Integrity::UNKNOWN;
}

QJsonObject RunStore::bundle(QString const &key, QString *error) {
  ScopedDb db(RUNS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query(QStringLiteral("SELECT %1, summary_json, samples_json FROM runs WHERE key = ?").arg(SELECT_COLUMNS), {key});
  if (!q.next()) {
    if (error) {
      *error = QStringLiteral("Run %1 not found").arg(key);
    }
    return {};
  }
  StoredRun const   run      = rowToRun(q);
  QByteArray const  samples  = q.value("samples_json").toString().toUtf8();
  QJsonObject const analysis = QJsonDocument::fromJson(q.value("summary_json").toString().toUtf8()).object();
  QJsonObject       manifestJson = run.manifest.toJson();
  QString const     integrity = verify(run.manifest, samples) == Integrity::VERIFIED   ? QStringLiteral("verified")
                                : verify(run.manifest, samples) == Integrity::MODIFIED ? QStringLiteral("modified")
                                                                                       : QStringLiteral("unknown");
  QJsonObject integrityJson = manifestJson.value("integrity").toObject();
  integrityJson.insert("status_when_exported", integrity);
  manifestJson.insert("integrity", integrityJson);
  manifestJson.insert("store", metadataJson(run));
  manifestJson.insert("analysis", analysis);
  manifestJson.insert("samples", QJsonDocument::fromJson(samples).array());
  // annotations and variable rules made on this run (the analysis page keys stored runs as "db:<key>")
  QString const runKey = QStringLiteral("db:%1").arg(key);
  QJsonArray    annotations;
  for (AnnotationRow const &row : AnnotationsDb::annotationsBetween(run.startEpoch, run.endEpoch, runKey)) {
    annotations.push_back(QJsonObject{{"start", row.t0}, {"end", row.t1}, {"label", row.label}, {"user", row.userName}, {"created_at", row.createdAt}});
  }
  QJsonArray rules;
  for (RuleRow const &row : AnnotationsDb::rules(runKey)) {
    rules.push_back(QJsonObject{{"channel", row.channel}, {"lo", std::isnan(row.lo) ? QJsonValue() : QJsonValue(row.lo)},
                                {"hi", std::isnan(row.hi) ? QJsonValue() : QJsonValue(row.hi)}, {"label", row.name}, {"user", row.userName}});
  }
  manifestJson.insert("annotations", annotations);
  manifestJson.insert("variable_rules", rules);
  return manifestJson;
}

} // namespace deepvaccontrol::storage
