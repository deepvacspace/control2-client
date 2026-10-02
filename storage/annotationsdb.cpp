#include "annotationsdb.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>

#include "sqlitedb.h"

namespace deepvaccontrol::storage {

namespace {
QVariant nullable(double value) {
  return std::isnan(value) ? QVariant() : QVariant(value);
}

QVariant nullable(QString const &value) {
  return value.isEmpty() ? QVariant() : QVariant(value);
}

// a null QString binds as SQL NULL; label / name are NOT NULL columns
QString notNull(QString const &text) {
  return text.isNull() ? QStringLiteral("") : text;
}

double toDouble(QVariant const &value) {
  return value.isNull() ? qQNaN() : value.toDouble();
}

bool ensureSchema(ScopedDb &db) {
  if (!db.isOpen()) {
    return false;
  }
  // identical to Insight's annotations_service.connect_annotations()
  db.exec(R"(CREATE TABLE IF NOT EXISTS annotations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            run_key TEXT NOT NULL,
            user_id INTEGER,
            user_name TEXT NOT NULL DEFAULT 'Unknown',
            x0 REAL NOT NULL,
            x1 REAL NOT NULL,
            label TEXT NOT NULL,
            color TEXT NOT NULL,
            created_at TEXT NOT NULL
        ))");
  db.exec("CREATE INDEX IF NOT EXISTS idx_annotations_run ON annotations(run_key)");
  db.exec(R"(CREATE TABLE IF NOT EXISTS variable_rules (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            run_key TEXT NOT NULL,
            user_id INTEGER,
            user_name TEXT NOT NULL DEFAULT 'Unknown',
            name TEXT NOT NULL,
            channel TEXT NOT NULL,
            lo REAL,
            hi REAL,
            color TEXT NOT NULL,
            created_at TEXT NOT NULL
        ))");
  db.exec("CREATE INDEX IF NOT EXISTS idx_rules_run ON variable_rules(run_key)");
  db.exec("CREATE TABLE IF NOT EXISTS collab_tombstones (uid TEXT PRIMARY KEY, kind TEXT NOT NULL DEFAULT 'annotation')");
  for (QString const &table : {QStringLiteral("annotations"), QStringLiteral("variable_rules")}) {
    if (!db.columns(table).contains("collab_uid")) {
      db.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN collab_uid TEXT").arg(table));
    }
  }
  if (!db.columns("collab_tombstones").contains("kind")) {
    db.exec("ALTER TABLE collab_tombstones ADD COLUMN kind TEXT NOT NULL DEFAULT 'annotation'");
  }

  // control-app extensions
  QStringList const annotationColumns = db.columns("annotations");
  if (!annotationColumns.contains("t0")) {
    db.exec("ALTER TABLE annotations ADD COLUMN t0 REAL");
  }
  if (!annotationColumns.contains("t1")) {
    db.exec("ALTER TABLE annotations ADD COLUMN t1 REAL");
  }
  db.exec("CREATE TABLE IF NOT EXISTS analysis_channels (run_key TEXT PRIMARY KEY, channels_json TEXT NOT NULL)");
  return true;
}

AnnotationRow readAnnotation(QSqlQuery const &q) {
  AnnotationRow row;
  row.id        = q.value("id").toLongLong();
  row.runKey    = q.value("run_key").toString();
  row.userName  = q.value("user_name").toString();
  row.x0        = q.value("x0").toDouble();
  row.x1        = q.value("x1").toDouble();
  row.t0        = toDouble(q.value("t0"));
  row.t1        = toDouble(q.value("t1"));
  row.label     = q.value("label").toString();
  row.color     = QColor(q.value("color").toString());
  row.createdAt = q.value("created_at").toString();
  row.collabUid = q.value("collab_uid").toString();
  return row;
}

RuleRow readRule(QSqlQuery const &q) {
  RuleRow row;
  row.id        = q.value("id").toLongLong();
  row.runKey    = q.value("run_key").toString();
  row.userName  = q.value("user_name").toString();
  row.name      = q.value("name").toString();
  row.channel   = q.value("channel").toString();
  row.lo        = toDouble(q.value("lo"));
  row.hi        = toDouble(q.value("hi"));
  row.color     = QColor(q.value("color").toString());
  row.createdAt = q.value("created_at").toString();
  row.collabUid = q.value("collab_uid").toString();
  return row;
}

void removeShared(QString const &table, QString const &kind, qint64 id, bool tombstone) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  if (tombstone) {
    QSqlQuery q = db.query(QStringLiteral("SELECT collab_uid FROM %1 WHERE id = ?").arg(table), {id});
    if (q.next() && !q.value(0).toString().isEmpty()) {
      db.exec("INSERT OR IGNORE INTO collab_tombstones (uid, kind) VALUES (?, ?)", {q.value(0).toString(), kind});
    }
  }
  db.exec(QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(table), {id});
}
} // namespace

QVector<AnnotationRow> AnnotationsDb::annotationsBetween(double start, double end, QString const &runKey) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  QVector<AnnotationRow> result;
  QSqlQuery q = db.query("SELECT * FROM annotations WHERE t1 >= ? AND t0 <= ? "
                         "AND (run_key NOT LIKE 'db:shared/%' OR run_key = ?) ORDER BY t0",
                         {start, end, runKey});
  while (q.next()) {
    result.push_back(readAnnotation(q));
  }
  return result;
}

QVector<AnnotationRow> AnnotationsDb::allAnnotations() {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  QVector<AnnotationRow> result;
  QSqlQuery              q = db.query("SELECT * FROM annotations ORDER BY id");
  while (q.next()) {
    result.push_back(readAnnotation(q));
  }
  return result;
}

qint64 AnnotationsDb::addAnnotation(AnnotationRow const &row) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query("INSERT INTO annotations (run_key, user_id, user_name, x0, x1, label, color, created_at, "
                         "collab_uid, t0, t1) VALUES (?, NULL, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                         {row.runKey,
                          row.userName.isEmpty() ? QStringLiteral("Unknown") : row.userName,
                          row.x0,
                          row.x1,
                          notNull(row.label),
                          row.color.name(),
                          row.createdAt.isEmpty() ? nowIso() : row.createdAt,
                          nullable(row.collabUid),
                          row.t0,
                          row.t1});
  return q.lastError().isValid() ? -1 : q.lastInsertId().toLongLong();
}

void AnnotationsDb::removeAnnotation(qint64 id) {
  removeShared("annotations", "annotation", id, true);
}

void AnnotationsDb::removeAnnotationWithoutTombstone(qint64 id) {
  removeShared("annotations", "annotation", id, false);
}

void AnnotationsDb::setAnnotationUid(qint64 id, QString const &uid) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  db.exec("UPDATE annotations SET collab_uid = ? WHERE id = ?", {uid, id});
}

QVector<RuleRow> AnnotationsDb::rules(QString const &runKey) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  QVector<RuleRow> result;
  QSqlQuery        q = db.query("SELECT * FROM variable_rules WHERE run_key = ? ORDER BY id", {runKey});
  while (q.next()) {
    result.push_back(readRule(q));
  }
  return result;
}

QVector<RuleRow> AnnotationsDb::allRules() {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  QVector<RuleRow> result;
  QSqlQuery        q = db.query("SELECT * FROM variable_rules ORDER BY id");
  while (q.next()) {
    result.push_back(readRule(q));
  }
  return result;
}

qint64 AnnotationsDb::addRule(RuleRow const &row) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query("INSERT INTO variable_rules (run_key, user_id, user_name, name, channel, lo, hi, color, "
                         "created_at, collab_uid) VALUES (?, NULL, ?, ?, ?, ?, ?, ?, ?, ?)",
                         {row.runKey,
                          row.userName.isEmpty() ? QStringLiteral("Unknown") : row.userName,
                          notNull(row.name),
                          row.channel,
                          nullable(row.lo),
                          nullable(row.hi),
                          row.color.name(),
                          row.createdAt.isEmpty() ? nowIso() : row.createdAt,
                          nullable(row.collabUid)});
  return q.lastError().isValid() ? -1 : q.lastInsertId().toLongLong();
}

void AnnotationsDb::removeRule(qint64 id) {
  removeShared("variable_rules", "variable_rule", id, true);
}

void AnnotationsDb::removeRuleWithoutTombstone(qint64 id) {
  removeShared("variable_rules", "variable_rule", id, false);
}

void AnnotationsDb::setRuleUid(qint64 id, QString const &uid) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  db.exec("UPDATE variable_rules SET collab_uid = ? WHERE id = ?", {uid, id});
}

QStringList AnnotationsDb::tombstones(QString const &kind) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  QStringList result;
  QSqlQuery   q = db.query("SELECT uid FROM collab_tombstones WHERE kind = ?", {kind});
  while (q.next()) {
    result.push_back(q.value(0).toString());
  }
  return result;
}

void AnnotationsDb::clearTombstone(QString const &uid) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  db.exec("DELETE FROM collab_tombstones WHERE uid = ?", {uid});
}

QStringList AnnotationsDb::channels(QString const &runKey) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  QSqlQuery q = db.query("SELECT channels_json FROM analysis_channels WHERE run_key = ?", {runKey});
  QStringList result;
  if (q.next()) {
    for (QJsonValue const &value : QJsonDocument::fromJson(q.value(0).toString().toUtf8()).array()) {
      result.push_back(value.toString());
    }
  }
  return result;
}

void AnnotationsDb::setChannels(QString const &runKey, QStringList const &channels) {
  ScopedDb db(ANNOTATIONS_DB);
  ensureSchema(db);
  db.exec("INSERT OR REPLACE INTO analysis_channels (run_key, channels_json) VALUES (?, ?)",
          {runKey, QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(channels)).toJson(QJsonDocument::Compact))});
}

void AnnotationsDb::migrateJsonNotes(QString const &jsonPath) {
  QFile file(jsonPath);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return;
  }
  QJsonObject const root = QJsonDocument::fromJson(file.readAll()).object();
  file.close();

  for (QJsonValue const &value : root["annotations"].toArray()) {
    QJsonObject const object = value.toObject();
    AnnotationRow     row;
    row.t0     = object["start"].toDouble();
    row.t1     = object["end"].toDouble();
    row.runKey = QStringLiteral("run-%1").arg(static_cast<qint64>(row.t0));
    row.x0     = 0.0;
    row.x1     = row.t1 - row.t0;
    row.label    = object["label"].toString();
    row.color    = QColor(object["color"].toString());
    row.userName = object["user"].toString();
    addAnnotation(row);
  }
  QJsonObject const rules = root["rules"].toObject();
  for (auto it = rules.begin(); it != rules.end(); ++it) {
    for (QJsonValue const &value : it.value().toArray()) {
      QJsonObject const object = value.toObject();
      RuleRow           row;
      row.runKey   = it.key();
      row.channel  = object["channel"].toString();
      row.lo       = object["lo"].isDouble() ? object["lo"].toDouble() : qQNaN();
      row.hi       = object["hi"].isDouble() ? object["hi"].toDouble() : qQNaN();
      row.name     = object["label"].toString();
      row.color    = QColor(object["color"].toString());
      row.userName = object["user"].toString();
      addRule(row);
    }
  }
  QJsonObject const channelsObject = root["channels"].toObject();
  for (auto it = channelsObject.begin(); it != channelsObject.end(); ++it) {
    setChannels(it.key(), it.value().toVariant().toStringList());
  }
  QFile::rename(jsonPath, jsonPath + ".migrated");
}

} // namespace deepvaccontrol::storage
