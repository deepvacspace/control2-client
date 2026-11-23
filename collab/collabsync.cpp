#include "collabsync.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>
#include <QUuid>
#include <cmath>
#include <optional>

#include "annotationsdb.h"
#include "runstore.h"
#include "sqlitedb.h"
#include "testprofilesstore.h"

namespace deepvaccontrol::collab {

using namespace deepvaccontrol::storage;

namespace {
constexpr qsizetype MAX_RUN_UPLOAD_BYTES{90'000'000};
QString const       DB_RUN_PREFIX{"db:"};

QString newUid() {
  return QUuid::createUuid().toString(QUuid::Id128);
}

QJsonValue number(double value) {
  return std::isnan(value) ? QJsonValue(QJsonValue::Null) : QJsonValue(value);
}

double toDouble(QJsonValue const &value) {
  return value.isDouble() ? value.toDouble() : qQNaN();
}

struct Context {
  CollabClient                *client;
  QVector<StoredRun>           runs;
  QHash<QString, StoredRun>    runByUid;
  QHash<QString, StoredRun>    runByKey;
  SyncResult                  *result;
};

// GET /items/<kind>: false (error recorded) when the request failed
bool fetch(Context &ctx, QString const &kind, QHash<QString, QJsonObject> &items, QSet<QString> &deleted) {
  QJsonObject response;
  QString     error;
  if (!ctx.client->request("GET", "/items/" + kind, nullptr, response, &error)) {
    ctx.result->errors.push_back(error);
    return false;
  }
  for (QJsonValue const &value : response.value("items").toArray()) {
    QJsonObject const item = value.toObject();
    items.insert(item.value("uid").toString(), item);
  }
  for (QJsonValue const &value : response.value("deleted").toArray()) {
    deleted.insert(value.toString());
  }
  return true;
}

bool post(Context &ctx, QString const &path, QJsonObject const &payload) {
  QJsonObject response;
  QString     error;
  if (!ctx.client->request("POST", path, &payload, response, &error)) {
    ctx.result->errors.push_back(error);
    return false;
  }
  return true;
}

void pushTombstones(Context &ctx, QString const &kind, QStringList const &uids, std::function<void(QString const &)> clear) {
  for (QString const &uid : uids) {
    if (post(ctx, QStringLiteral("/items/%1/%2/delete").arg(kind, uid), QJsonObject{})) {
      clear(uid);
    }
  }
}

// run an annotation belongs to: its database run, else a run recorded here covering it
std::optional<StoredRun> runForAnnotation(Context const &ctx, AnnotationRow const &row) {
  if (row.runKey.startsWith(DB_RUN_PREFIX)) {
    auto it = ctx.runByKey.constFind(row.runKey.mid(DB_RUN_PREFIX.size()));
    return (it != ctx.runByKey.constEnd()) ? std::optional<StoredRun>(*it) : std::nullopt;
  }
  for (StoredRun const &run : ctx.runs) {
    if ((run.source == "monitoring") && !run.runUid.isEmpty() && (run.startEpoch <= row.t0) && (run.endEpoch >= row.t1)) {
      return run;
    }
  }
  return std::nullopt;
}

void syncAnnotations(Context &ctx) {
  QString const kind{"annotation"};
  KindResult   &kindResult = ctx.result->kinds[kind];
  pushTombstones(ctx, kind, AnnotationsDb::tombstones(kind), [](QString const &uid) {
    AnnotationsDb::clearTombstone(uid);
  });
  QHash<QString, QJsonObject> host;
  QSet<QString>               deleted;
  if (!fetch(ctx, kind, host, deleted)) {
    return;
  }

  QSet<QString> localUids;
  for (AnnotationRow const &row : AnnotationsDb::allAnnotations()) {
    if (!row.collabUid.isEmpty() && deleted.contains(row.collabUid)) {
      AnnotationsDb::removeAnnotationWithoutTombstone(row.id);
      ++kindResult.removed;
      continue;
    }
    if (!row.collabUid.isEmpty()) {
      localUids.insert(row.collabUid);
      if (host.contains(row.collabUid)) {
        continue;
      }
    }
    auto const run = runForAnnotation(ctx, row);
    if (!run) {
      ++kindResult.skipped; // only annotations on saved experiments can be shared
      continue;
    }
    QString const     uid = row.collabUid.isEmpty() ? newUid() : row.collabUid;
    QJsonObject const payload{{"uid", uid},
                              {"run_uid", run->runUid},
                              {"user_name", row.userName},
                              {"x0", row.t0 - run->startEpoch},
                              {"x1", row.t1 - run->startEpoch},
                              {"label", row.label},
                              {"color", row.color.name()},
                              {"created_at", row.createdAt}};
    if (post(ctx, "/items/" + kind, payload)) {
      AnnotationsDb::setAnnotationUid(row.id, uid);
      localUids.insert(uid);
      ++kindResult.pushed;
    }
  }

  QStringList const   tombstoneList = AnnotationsDb::tombstones(kind);
  QSet<QString> const tombstoned(tombstoneList.cbegin(), tombstoneList.cend());
  for (auto it = host.cbegin(); it != host.cend(); ++it) {
    if (localUids.contains(it.key()) || tombstoned.contains(it.key())) {
      continue;
    }
    auto const run = ctx.runByUid.constFind(it->value("run_uid").toString());
    if ((run == ctx.runByUid.constEnd()) || std::isnan(run->startEpoch)) {
      ++kindResult.skipped; // the experiment is not on this computer
      continue;
    }
    AnnotationRow row;
    row.runKey    = DB_RUN_PREFIX + run->key;
    row.userName  = it->value("user_name").toString();
    row.x0        = it->value("x0").toDouble();
    row.x1        = it->value("x1").toDouble();
    row.t0        = run->startEpoch + row.x0;
    row.t1        = run->startEpoch + row.x1;
    row.label     = it->value("label").toString();
    row.color     = QColor(it->value("color").toString());
    row.createdAt = it->value("created_at").toString();
    row.collabUid = it.key();
    AnnotationsDb::addAnnotation(row);
    ++kindResult.pulled;
  }
}

void syncRules(Context &ctx) {
  QString const kind{"variable_rule"};
  KindResult   &kindResult = ctx.result->kinds[kind];
  pushTombstones(ctx, kind, AnnotationsDb::tombstones(kind), [](QString const &uid) {
    AnnotationsDb::clearTombstone(uid);
  });
  QHash<QString, QJsonObject> host;
  QSet<QString>               deleted;
  if (!fetch(ctx, kind, host, deleted)) {
    return;
  }

  QSet<QString> localUids;
  for (RuleRow const &row : AnnotationsDb::allRules()) {
    if (!row.collabUid.isEmpty() && deleted.contains(row.collabUid)) {
      AnnotationsDb::removeRuleWithoutTombstone(row.id);
      ++kindResult.removed;
      continue;
    }
    if (!row.collabUid.isEmpty()) {
      localUids.insert(row.collabUid);
      if (host.contains(row.collabUid)) {
        continue;
      }
    }
    auto const run = row.runKey.startsWith(DB_RUN_PREFIX) ? ctx.runByKey.constFind(row.runKey.mid(DB_RUN_PREFIX.size()))
                                                          : ctx.runByKey.constEnd();
    if ((run == ctx.runByKey.constEnd()) || run->runUid.isEmpty()) {
      ++kindResult.skipped; // only rules on saved experiments can be shared
      continue;
    }
    QString const     uid = row.collabUid.isEmpty() ? newUid() : row.collabUid;
    QJsonObject const payload{{"uid", uid},
                              {"run_uid", run->runUid},
                              {"user_name", row.userName},
                              {"name", row.name},
                              {"channel", row.channel},
                              {"lo", number(row.lo)},
                              {"hi", number(row.hi)},
                              {"color", row.color.name()},
                              {"created_at", row.createdAt}};
    if (post(ctx, "/items/" + kind, payload)) {
      AnnotationsDb::setRuleUid(row.id, uid);
      localUids.insert(uid);
      ++kindResult.pushed;
    }
  }

  QStringList const   tombstoneList = AnnotationsDb::tombstones(kind);
  QSet<QString> const tombstoned(tombstoneList.cbegin(), tombstoneList.cend());
  for (auto it = host.cbegin(); it != host.cend(); ++it) {
    if (localUids.contains(it.key()) || tombstoned.contains(it.key())) {
      continue;
    }
    auto const run = ctx.runByUid.constFind(it->value("run_uid").toString());
    if (run == ctx.runByUid.constEnd()) {
      ++kindResult.skipped;
      continue;
    }
    RuleRow row;
    row.runKey    = DB_RUN_PREFIX + run->key;
    row.userName  = it->value("user_name").toString();
    row.name      = it->value("name").toString();
    row.channel   = it->value("channel").toString();
    row.lo        = toDouble(it->value("lo"));
    row.hi        = toDouble(it->value("hi"));
    row.color     = QColor(it->value("color").toString());
    row.createdAt = it->value("created_at").toString();
    row.collabUid = it.key();
    AnnotationsDb::addRule(row);
    ++kindResult.pulled;
  }
}

QJsonObject runFields(StoredRun const &run) {
  QJsonObject fields = RunStore::metadataJson(run);
  return fields;
}

void replaceSharedRuns(QHash<QString, QJsonObject> const &host, QSet<QString> const &withData) {
  ScopedDb db(SHARED_RUNS_DB);
  db.exec("CREATE TABLE IF NOT EXISTS shared_runs (run_uid TEXT PRIMARY KEY, data TEXT NOT NULL)");
  db.transaction();
  db.exec("DELETE FROM shared_runs");
  for (auto it = host.cbegin(); it != host.cend(); ++it) {
    QJsonObject data = it.value();
    data.remove("uid");
    data.insert("has_data", withData.contains(it.key())); // control-app extension
    db.exec("INSERT INTO shared_runs (run_uid, data) VALUES (?, ?)",
            {it.key(), QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))});
  }
  db.commit();
}

void syncRuns(Context &ctx, QString const &userName) {
  KindResult                 &metadataResult = ctx.result->kinds["run_metadata"];
  KindResult                 &dataResult     = ctx.result->kinds["run_data"];
  QHash<QString, QJsonObject> host;
  QSet<QString>               deleted;
  if (!fetch(ctx, "run_metadata", host, deleted)) {
    return;
  }

  // experiments recorded on this computer are shared; downloaded ones already are
  for (StoredRun const &run : std::as_const(ctx.runs)) {
    if ((run.source != "monitoring") || run.runUid.isEmpty()) {
      continue;
    }
    QJsonObject fields = runFields(run);
    auto const  remote = host.constFind(run.runUid);
    bool        same{remote != host.constEnd()};
    for (auto it = fields.constBegin(); same && (it != fields.constEnd()); ++it) {
      same = (remote->value(it.key()) == it.value());
    }
    if (same) {
      continue;
    }
    fields.insert("uid", run.runUid);
    fields.insert("shared_by", userName);
    if (post(ctx, "/items/run_metadata", fields)) {
      host.insert(run.runUid, fields);
      ++metadataResult.pushed;
    }
  }
  metadataResult.pulled = 0;
  for (auto it = host.cbegin(); it != host.cend(); ++it) {
    metadataResult.pulled += ctx.runByUid.contains(it.key()) ? 0 : 1;
  }

  // upload the samples of local experiments the host does not have yet
  QJsonObject   response;
  QString       error;
  QSet<QString> withData;
  if (!ctx.client->request("GET", "/runs", nullptr, response, &error)) {
    ctx.result->errors.push_back(error);
  } else {
    for (QJsonValue const &uid : response.value("uids").toArray()) {
      withData.insert(uid.toString());
    }
    for (StoredRun const &run : std::as_const(ctx.runs)) {
      if ((run.source != "monitoring") || run.runUid.isEmpty() || withData.contains(run.runUid)) {
        continue;
      }
      QByteArray const compressed = qCompress(RunStore::samplesJson(run.key), 6);
      if (compressed.size() > MAX_RUN_UPLOAD_BYTES) {
        ++dataResult.skipped;
        continue;
      }
      QJsonObject metadata = runFields(run);
      metadata.insert("uid", run.runUid);
      if (run.manifest.isValid()) {
        metadata.insert("manifest", run.manifest.toJson()); // configuration, events, content hash: verified on import
      }
      QJsonObject const payload{{"metadata", metadata}, {"samples_z", QString::fromLatin1(compressed.toBase64())}};
      QJsonObject       uploadResponse;
      if (ctx.client->request("POST", "/runs/" + run.runUid, &payload, uploadResponse, &error, nullptr, 300'000)) {
        withData.insert(run.runUid);
        ++dataResult.pushed;
      } else {
        ctx.result->errors.push_back(error);
      }
    }
  }
  replaceSharedRuns(host, withData);
}

void syncTestProfiles(Context &ctx) {
  QString const kind{"test_profile"};
  KindResult   &kindResult = ctx.result->kinds[kind];
  pushTombstones(ctx, kind, TestProfilesStore::tombstones(), [](QString const &uid) {
    TestProfilesStore::clearTombstone(uid);
  });
  QHash<QString, QJsonObject> host;
  QSet<QString>               deleted;
  if (!fetch(ctx, kind, host, deleted)) {
    return;
  }
  QStringList const   tombstoneList = TestProfilesStore::tombstones();
  QSet<QString> const tombstoned(tombstoneList.cbegin(), tombstoneList.cend());

  QSet<QString> localUids;
  for (TestProfile const &profile : TestProfilesStore::list()) {
    if (!profile.collabUid.isEmpty() && deleted.contains(profile.collabUid)) {
      TestProfilesStore::removeWithoutTombstone(profile.id);
      ++kindResult.removed;
      continue;
    }
    QString const uid    = profile.collabUid.isEmpty() ? newUid() : profile.collabUid;
    auto const    remote = host.constFind(uid);
    localUids.insert(uid);
    if ((remote != host.constEnd()) && (remote->value("updated_at").toString() > profile.updatedAt)) {
      if (TestProfilesStore::upsertFromRemote(*remote)) { // newer on another computer
        ++kindResult.pulled;
      }
      continue;
    }
    if ((remote != host.constEnd()) && (remote->value("updated_at").toString() == profile.updatedAt)) {
      continue;
    }
    QJsonObject payload = TestProfilesStore::toJson(profile);
    payload.insert("uid", uid);
    if (post(ctx, "/items/" + kind, payload)) {
      if (profile.collabUid.isEmpty()) {
        TestProfilesStore::setCollabUid(profile.id, uid);
      }
      ++kindResult.pushed;
    }
  }

  for (auto it = host.cbegin(); it != host.cend(); ++it) {
    if (localUids.contains(it.key()) || tombstoned.contains(it.key())) {
      continue;
    }
    QString error;
    if (TestProfilesStore::upsertFromRemote(*it, &error)) {
      ++kindResult.pulled;
    } else {
      ++kindResult.skipped;
    }
  }
}
}

QString SyncResult::summary() const {
  static QMap<QString, QString> const names{{"annotation", QObject::tr("Annotations")},
                                            {"variable_rule", QObject::tr("Variable rules")},
                                            {"run_metadata", QObject::tr("Experiment list")},
                                            {"run_data", QObject::tr("Experiment data")},
                                            {"test_profile", QObject::tr("Test profiles")}};
  QStringList lines;
  for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
    QString const name = names.value(it.key(), it.key());
    lines.push_back(QObject::tr("%1: sent %2, received %3, removed %4, skipped %5")
                      .arg(name)
                      .arg(it->pushed)
                      .arg(it->pulled)
                      .arg(it->removed)
                      .arg(it->skipped));
  }
  if (!errors.isEmpty()) {
    QStringList unique = errors;
    unique.removeDuplicates();
    lines.push_back(QObject::tr("Errors: %1").arg(unique.mid(0, 3).join("; ")));
  }
  return lines.join('\n');
}

SyncResult CollabSync::sync(Connection const &connection, QString const &userName) {
  SyncResult   result;
  CollabClient client(connection);
  Context      ctx{&client, RunStore::list(), {}, {}, &result};
  for (StoredRun const &run : std::as_const(ctx.runs)) {
    if (!run.runUid.isEmpty()) {
      ctx.runByUid.insert(run.runUid, run);
    }
    ctx.runByKey.insert(run.key, run);
  }

  syncAnnotations(ctx);
  syncRules(ctx);
  syncRuns(ctx, userName);
  syncTestProfiles(ctx);
  return result;
}

QVector<SharedRun> CollabSync::sharedRuns() {
  QSet<QString> localUids;
  for (StoredRun const &run : RunStore::list()) {
    localUids.insert(run.runUid);
  }
  ScopedDb db(SHARED_RUNS_DB);
  db.exec("CREATE TABLE IF NOT EXISTS shared_runs (run_uid TEXT PRIMARY KEY, data TEXT NOT NULL)");
  QVector<SharedRun> result;
  QSqlQuery          q = db.query("SELECT run_uid, data FROM shared_runs");
  while (q.next()) {
    SharedRun run;
    run.uid      = q.value(0).toString();
    run.metadata = QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object();
    run.hasData  = run.metadata.value("has_data").toBool();
    run.local    = localUids.contains(run.uid);
    result.push_back(run);
  }
  std::sort(result.begin(), result.end(), [](SharedRun const &a, SharedRun const &b) {
    return a.metadata.value("start_time").toString() > b.metadata.value("start_time").toString();
  });
  return result;
}

QString CollabSync::downloadRun(Connection const &connection, QString const &uid, QString *error) {
  CollabClient client(connection);
  QJsonObject  response;
  if (!client.request("GET", "/runs/" + uid, nullptr, response, error, nullptr, 300'000)) {
    return {};
  }
  QByteArray const samples = qUncompress(QByteArray::fromBase64(response.value("samples_z").toString().toLatin1()));
  if (samples.isEmpty()) {
    if (error) {
      *error = QObject::tr("The host sent damaged experiment data.");
    }
    return {};
  }
  QJsonObject metadata = response.value("metadata").toObject();
  metadata.insert("uid", uid);
  return RunStore::importShared(metadata, samples, error);
}

}
