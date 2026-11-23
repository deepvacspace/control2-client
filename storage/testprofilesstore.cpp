#include "testprofilesstore.h"

#include <QJsonArray>
#include <QSqlError>
#include <QSqlQuery>
#include <cmath>

#include "sqlitedb.h"

namespace deepvaccontrol::storage {

namespace {
QVariant nullable(double value) {
  return std::isnan(value) ? QVariant() : QVariant(value);
}

// a null QString binds as SQL NULL; description / label are NOT NULL columns
QString notNull(QString const &text) {
  return text.isNull() ? QStringLiteral("") : text;
}

bool ensureSchema(ScopedDb &db) {
  if (!db.isOpen()) {
    return false;
  }
  // identical to Insight's test_profiles_service.connect_test_profiles()
  db.exec(R"(CREATE TABLE IF NOT EXISTS test_profiles (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            description TEXT NOT NULL DEFAULT '',
            created_by TEXT NOT NULL DEFAULT 'Unknown',
            created_at TEXT NOT NULL,
            hub_id TEXT UNIQUE,
            hub_synced_at TEXT
        ))");
  QStringList const existing = db.columns("test_profiles");
  for (QString const &column : {QStringLiteral("hub_id"), QStringLiteral("hub_synced_at")}) {
    if (!existing.contains(column)) {
      db.exec(QStringLiteral("ALTER TABLE test_profiles ADD COLUMN %1 TEXT").arg(column));
    }
  }
  db.exec(R"(CREATE TABLE IF NOT EXISTS test_profile_steps (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            profile_id INTEGER NOT NULL REFERENCES test_profiles(id) ON DELETE CASCADE,
            step_order INTEGER NOT NULL,
            setpoint_temp REAL,
            setpoint_pressure REAL,
            duration_s REAL NOT NULL,
            label TEXT NOT NULL DEFAULT ''
        ))");
  db.exec("CREATE INDEX IF NOT EXISTS idx_steps_profile_order ON test_profile_steps(profile_id, step_order)");

  // control-app extensions for local-network sharing
  if (!existing.contains("collab_uid")) {
    db.exec("ALTER TABLE test_profiles ADD COLUMN collab_uid TEXT");
  }
  if (!existing.contains("updated_at")) {
    db.exec("ALTER TABLE test_profiles ADD COLUMN updated_at TEXT");
  }
  if (!existing.contains("program_json")) {
    db.exec("ALTER TABLE test_profiles ADD COLUMN program_json TEXT");
  }
  db.exec("CREATE TABLE IF NOT EXISTS collab_tombstones (uid TEXT PRIMARY KEY)");
  return true;
}

double toDouble(QVariant const &value) {
  return value.isNull() ? qQNaN() : value.toDouble();
}

TestProfile readProfile(ScopedDb &db, QSqlQuery const &row) {
  TestProfile profile;
  profile.id          = row.value("id").toLongLong();
  profile.name        = row.value("name").toString();
  profile.description = row.value("description").toString();
  profile.createdBy   = row.value("created_by").toString();
  profile.createdAt   = row.value("created_at").toString();
  profile.collabUid   = row.value("collab_uid").toString();
  profile.updatedAt   = row.value("updated_at").toString();
  profile.programJson = row.value("program_json").toString();
  if (profile.updatedAt.isEmpty()) {
    profile.updatedAt = profile.createdAt;
  }

  QSqlQuery steps = db.query("SELECT * FROM test_profile_steps WHERE profile_id = ? ORDER BY step_order", {profile.id});
  while (steps.next()) {
    profile.steps.push_back({toDouble(steps.value("setpoint_temp")),
                             toDouble(steps.value("setpoint_pressure")),
                             steps.value("duration_s").toDouble(),
                             steps.value("label").toString()});
  }
  return profile;
}

std::optional<TestProfile> selectOne(QString const &where, QVariant const &value) {
  ScopedDb db(TEST_PROFILES_DB);
  if (!ensureSchema(db)) {
    return std::nullopt;
  }
  QSqlQuery q = db.query(QStringLiteral("SELECT * FROM test_profiles WHERE %1 = ?").arg(where), {value});
  if (!q.next()) {
    return std::nullopt;
  }
  return readProfile(db, q);
}

bool insertSteps(ScopedDb &db, qint64 profileId, QVector<TestProfileStep> const &steps) {
  for (int order{0}; order < steps.size(); ++order) {
    TestProfileStep const &step = steps[order];
    if (!db.exec("INSERT INTO test_profile_steps (profile_id, step_order, setpoint_temp, setpoint_pressure, "
                 "duration_s, label) VALUES (?, ?, ?, ?, ?, ?)",
                 {profileId, order, nullable(step.setpointTemp), nullable(step.setpointPressure), step.durationS, notNull(step.label)})) {
      return false;
    }
  }
  return true;
}

bool isUniqueViolation(QString const &error) {
  return error.contains("UNIQUE", Qt::CaseInsensitive);
}
}

double TestProfile::totalDurationS() const {
  double total{0.0};
  for (TestProfileStep const &step : steps) {
    total += step.durationS;
  }
  return total;
}

QVector<TestProfile> TestProfilesStore::list(QString *error) {
  ScopedDb db(TEST_PROFILES_DB);
  if (!ensureSchema(db)) {
    if (error) {
      *error = db.lastError();
    }
    return {};
  }
  QVector<TestProfile> result;
  QSqlQuery            q = db.query("SELECT * FROM test_profiles ORDER BY name");
  while (q.next()) {
    result.push_back(readProfile(db, q));
  }
  return result;
}

std::optional<TestProfile> TestProfilesStore::get(qint64 id) {
  return selectOne("id", id);
}

std::optional<TestProfile> TestProfilesStore::findByName(QString const &name) {
  return selectOne("name", name.trimmed());
}

std::optional<TestProfile> TestProfilesStore::findByCollabUid(QString const &uid) {
  return uid.isEmpty() ? std::nullopt : selectOne("collab_uid", uid);
}

bool TestProfilesStore::validate(QString const &name, QVector<TestProfileStep> const &steps, QString *error, bool hasProgram) {
  auto fail = [error](QString const &text) {
    if (error) {
      *error = text;
    }
    return false;
  };
  if (name.trimmed().isEmpty()) {
    return fail(QObject::tr("Name is required."));
  }
  if (steps.isEmpty()) {
    return fail(QObject::tr("A test profile needs at least one step."));
  }
  for (int i{0}; i < steps.size(); ++i) {
    if (!hasProgram && std::isnan(steps[i].setpointTemp) && std::isnan(steps[i].setpointPressure)) {
      return fail(QObject::tr("Step %1 needs at least one setpoint (temperature or pressure).").arg(i + 1));
    }
    if (!(steps[i].durationS > 0.0)) {
      return fail(QObject::tr("Step %1's duration must be greater than 0 seconds.").arg(i + 1));
    }
  }
  return true;
}

std::optional<TestProfile> TestProfilesStore::add(QString const &name, QString const &description,
                                                  QVector<TestProfileStep> const &steps, QString const &createdBy,
                                                  QString *error, QString const &programJson) {
  if (!validate(name, steps, error, !programJson.isEmpty())) {
    return std::nullopt;
  }
  qint64 id{-1};
  {
    ScopedDb db(TEST_PROFILES_DB);
    if (!ensureSchema(db) || !db.transaction()) {
      if (error) {
        *error = db.lastError();
      }
      return std::nullopt;
    }
    QString const now = nowIso();
    QSqlQuery     q   = db.query("INSERT INTO test_profiles (name, description, created_by, created_at, updated_at, program_json) "
                                 "VALUES (?, ?, ?, ?, ?, ?)",
                                 {name.trimmed(), notNull(description), createdBy.isEmpty() ? QStringLiteral("Unknown") : createdBy, now, now,
                                  programJson.isEmpty() ? QVariant() : QVariant(programJson)});
    if (q.lastError().isValid()) {
      db.rollback();
      if (error) {
        *error = isUniqueViolation(q.lastError().text())
                   ? QObject::tr("A test profile named '%1' already exists.").arg(name.trimmed())
                   : q.lastError().text();
      }
      return std::nullopt;
    }
    id = q.lastInsertId().toLongLong();
    if (!insertSteps(db, id, steps) || !db.commit()) {
      db.rollback();
      if (error) {
        *error = db.lastError();
      }
      return std::nullopt;
    }
  }
  return get(id);
}

std::optional<TestProfile> TestProfilesStore::update(qint64 id, QString const &name, QString const &description,
                                                     QVector<TestProfileStep> const &steps, QString *error,
                                                     QString const &programJson) {
  if (!validate(name, steps, error, !programJson.isEmpty())) {
    return std::nullopt;
  }
  {
    ScopedDb db(TEST_PROFILES_DB);
    if (!ensureSchema(db) || !db.transaction()) {
      if (error) {
        *error = db.lastError();
      }
      return std::nullopt;
    }
    QSqlQuery q = db.query("UPDATE test_profiles SET name = ?, description = ?, updated_at = ?, program_json = ? WHERE id = ?",
                           {name.trimmed(), notNull(description), nowIso(), programJson.isEmpty() ? QVariant() : QVariant(programJson), id});
    if (q.lastError().isValid()) {
      db.rollback();
      if (error) {
        *error = isUniqueViolation(q.lastError().text())
                   ? QObject::tr("A test profile named '%1' already exists.").arg(name.trimmed())
                   : q.lastError().text();
      }
      return std::nullopt;
    }
    if (q.numRowsAffected() == 0) {
      db.rollback();
      if (error) {
        *error = QObject::tr("Test profile not found.");
      }
      return std::nullopt;
    }
    db.exec("DELETE FROM test_profile_steps WHERE profile_id = ?", {id});
    if (!insertSteps(db, id, steps) || !db.commit()) {
      db.rollback();
      if (error) {
        *error = db.lastError();
      }
      return std::nullopt;
    }
  }
  return get(id);
}

bool TestProfilesStore::remove(qint64 id) {
  ScopedDb db(TEST_PROFILES_DB);
  ensureSchema(db);
  // remember shared profiles so the deletion reaches the other computers
  QSqlQuery q = db.query("SELECT collab_uid FROM test_profiles WHERE id = ?", {id});
  if (q.next() && !q.value(0).toString().isEmpty()) {
    db.exec("INSERT OR IGNORE INTO collab_tombstones (uid) VALUES (?)", {q.value(0).toString()});
  }
  db.exec("DELETE FROM test_profile_steps WHERE profile_id = ?", {id});
  return db.exec("DELETE FROM test_profiles WHERE id = ?", {id});
}

bool TestProfilesStore::removeWithoutTombstone(qint64 id) {
  ScopedDb db(TEST_PROFILES_DB);
  ensureSchema(db);
  db.exec("DELETE FROM test_profile_steps WHERE profile_id = ?", {id});
  return db.exec("DELETE FROM test_profiles WHERE id = ?", {id});
}

bool TestProfilesStore::setCollabUid(qint64 id, QString const &uid) {
  ScopedDb db(TEST_PROFILES_DB);
  ensureSchema(db);
  return db.exec("UPDATE test_profiles SET collab_uid = ? WHERE id = ?", {uid, id});
}

QStringList TestProfilesStore::tombstones() {
  ScopedDb db(TEST_PROFILES_DB);
  ensureSchema(db);
  QStringList result;
  QSqlQuery   q = db.query("SELECT uid FROM collab_tombstones");
  while (q.next()) {
    result.push_back(q.value(0).toString());
  }
  return result;
}

void TestProfilesStore::clearTombstone(QString const &uid) {
  ScopedDb db(TEST_PROFILES_DB);
  ensureSchema(db);
  db.exec("DELETE FROM collab_tombstones WHERE uid = ?", {uid});
}

QJsonObject TestProfilesStore::toJson(TestProfile const &profile) {
  QJsonArray steps;
  for (TestProfileStep const &step : profile.steps) {
    steps.push_back(QJsonObject{
      {"setpoint_temp", std::isnan(step.setpointTemp) ? QJsonValue(QJsonValue::Null) : QJsonValue(step.setpointTemp)},
      {"setpoint_pressure",
       std::isnan(step.setpointPressure) ? QJsonValue(QJsonValue::Null) : QJsonValue(step.setpointPressure)},
      {"duration_s", step.durationS},
      {"label", step.label}});
  }
  return QJsonObject{{"name", profile.name},
                     {"description", profile.description},
                     {"created_by", profile.createdBy},
                     {"created_at", profile.createdAt},
                     {"updated_at", profile.updatedAt},
                     {"program_json", profile.programJson},
                     {"steps", steps}};
}

bool TestProfilesStore::upsertFromRemote(QJsonObject const &item, QString *error) {
  QString const uid = item.value("uid").toString();
  QVector<TestProfileStep> steps;
  for (QJsonValue const &value : item.value("steps").toArray()) {
    QJsonObject const step = value.toObject();
    steps.push_back({step.value("setpoint_temp").isDouble() ? step.value("setpoint_temp").toDouble() : qQNaN(),
                     step.value("setpoint_pressure").isDouble() ? step.value("setpoint_pressure").toDouble() : qQNaN(),
                     step.value("duration_s").toDouble(),
                     step.value("label").toString()});
  }
  QString name = item.value("name").toString().trimmed();
  QString const programJson = item.value("program_json").toString();
  if (!validate(name, steps, error, !programJson.isEmpty())) {
    return false;
  }

  ScopedDb db(TEST_PROFILES_DB);
  if (!ensureSchema(db) || !db.transaction()) {
    return false;
  }
  qint64    id{-1};
  QSqlQuery existing = db.query("SELECT id FROM test_profiles WHERE collab_uid = ?", {uid});
  if (existing.next()) {
    id = existing.value(0).toLongLong();
  }

  // a different local profile may already use the name
  QString   uniqueName = name;
  for (int n{2};; ++n) {
    QSqlQuery clash = db.query("SELECT id FROM test_profiles WHERE name = ? AND id != ?", {uniqueName, id});
    if (!clash.next()) {
      break;
    }
    uniqueName = QStringLiteral("%1 (%2)").arg(name).arg(n);
  }

  bool ok{false};
  if (id >= 0) {
    ok = db.exec("UPDATE test_profiles SET name = ?, description = ?, updated_at = ?, program_json = ? WHERE id = ?",
                 {uniqueName, notNull(item.value("description").toString()), item.value("updated_at").toString(),
                  programJson.isEmpty() ? QVariant() : QVariant(programJson), id})
         && db.exec("DELETE FROM test_profile_steps WHERE profile_id = ?", {id});
  } else {
    QSqlQuery q = db.query("INSERT INTO test_profiles (name, description, created_by, created_at, updated_at, collab_uid, "
                           "program_json) VALUES (?, ?, ?, ?, ?, ?, ?)",
                           {uniqueName,
                            notNull(item.value("description").toString()),
                            item.value("created_by").toString("Unknown"),
                            item.value("created_at").toString(nowIso()),
                            item.value("updated_at").toString(nowIso()),
                            uid,
                            programJson.isEmpty() ? QVariant() : QVariant(programJson)});
    ok = !q.lastError().isValid();
    id = q.lastInsertId().toLongLong();
  }
  ok = ok && insertSteps(db, id, steps) && db.commit();
  if (!ok) {
    db.rollback();
    if (error) {
      *error = db.lastError();
    }
  }
  return ok;
}

}
