#include "collabhoststore.h"

#include <QJsonDocument>
#include <QSqlQuery>

#include "collabcrypto.h"
#include "sqlitedb.h"

namespace deepvaccontrol::collab {

using storage::COLLAB_HOST_DB;
using storage::nowIso;
using storage::ScopedDb;

namespace {
bool ensureSchema(ScopedDb &db) {
  if (!db.isOpen()) {
    return false;
  }
  db.exec(R"(CREATE TABLE IF NOT EXISTS peers (
            key_hash TEXT PRIMARY KEY,
            public_key BLOB NOT NULL,
            display_name TEXT NOT NULL,
            paired_at TEXT NOT NULL
        ))");
  db.exec(R"(CREATE TABLE IF NOT EXISTS items (
            uid TEXT PRIMARY KEY,
            kind TEXT NOT NULL,
            data TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            deleted_at TEXT
        ))");
  db.exec("CREATE INDEX IF NOT EXISTS idx_collab_items_kind ON items(kind)");
  db.exec(R"(CREATE TABLE IF NOT EXISTS run_data (
            uid TEXT PRIMARY KEY,
            metadata TEXT NOT NULL,
            samples BLOB NOT NULL,
            updated_at TEXT NOT NULL
        ))");
  return true;
}

QString compactJson(QJsonObject const &object) {
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
} // namespace

QString CollabHostStore::registerPeer(QByteArray const &publicKey, QString const &displayName) {
  QString const keyHash = CollabCrypto::keyHash(publicKey);
  ScopedDb      db(COLLAB_HOST_DB);
  ensureSchema(db);
  db.exec("INSERT INTO peers (key_hash, public_key, display_name, paired_at) VALUES (?, ?, ?, ?) "
          "ON CONFLICT(key_hash) DO UPDATE SET display_name = excluded.display_name",
          {keyHash, publicKey, displayName.isEmpty() ? QStringLiteral("Unknown") : displayName, nowIso()});
  return keyHash;
}

std::optional<Peer> CollabHostStore::peer(QString const &keyHash) {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  QSqlQuery q = db.query("SELECT * FROM peers WHERE key_hash = ?", {keyHash});
  if (!q.next()) {
    return std::nullopt;
  }
  return Peer{q.value("key_hash").toString(), q.value("public_key").toByteArray(), q.value("display_name").toString(),
              q.value("paired_at").toString()};
}

QVector<Peer> CollabHostStore::peers() {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  QVector<Peer> result;
  QSqlQuery     q = db.query("SELECT * FROM peers ORDER BY paired_at");
  while (q.next()) {
    result.push_back({q.value("key_hash").toString(), q.value("public_key").toByteArray(), q.value("display_name").toString(),
                      q.value("paired_at").toString()});
  }
  return result;
}

bool CollabHostStore::removePeer(QString const &keyHash) {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  return db.exec("DELETE FROM peers WHERE key_hash = ?", {keyHash});
}

QJsonArray CollabHostStore::listItems(QString const &kind) {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  QJsonArray result;
  QSqlQuery  q = db.query("SELECT uid, data FROM items WHERE kind = ? AND deleted_at IS NULL ORDER BY updated_at", {kind});
  while (q.next()) {
    QJsonObject item = QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object();
    item.insert("uid", q.value(0).toString());
    result.push_back(item);
  }
  return result;
}

QStringList CollabHostStore::listDeletedUids(QString const &kind) {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  QStringList result;
  QSqlQuery   q = db.query("SELECT uid FROM items WHERE kind = ? AND deleted_at IS NOT NULL", {kind});
  while (q.next()) {
    result.push_back(q.value(0).toString());
  }
  return result;
}

void CollabHostStore::putItem(QString const &kind, QString const &uid, QJsonObject const &data, bool mutableKind) {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  if (mutableKind) {
    db.exec("INSERT INTO items (uid, kind, data, updated_at) VALUES (?, ?, ?, ?) "
            "ON CONFLICT(uid) DO UPDATE SET data = excluded.data, updated_at = excluded.updated_at "
            "WHERE items.kind = excluded.kind AND items.deleted_at IS NULL",
            {uid, kind, compactJson(data), nowIso()});
  } else {
    db.exec("INSERT OR IGNORE INTO items (uid, kind, data, updated_at) VALUES (?, ?, ?, ?)",
            {uid, kind, compactJson(data), nowIso()});
  }
}

bool CollabHostStore::deleteItem(QString const &kind, QString const &uid) {
  ScopedDb  db(COLLAB_HOST_DB);
  ensureSchema(db);
  QSqlQuery q = db.query("UPDATE items SET deleted_at = COALESCE(deleted_at, ?) WHERE uid = ? AND kind = ?", {nowIso(), uid, kind});
  return q.numRowsAffected() > 0;
}

QStringList CollabHostStore::runDataUids() {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  QStringList result;
  QSqlQuery   q = db.query("SELECT uid FROM run_data");
  while (q.next()) {
    result.push_back(q.value(0).toString());
  }
  return result;
}

bool CollabHostStore::putRunData(QString const &uid, QJsonObject const &metadata, QByteArray const &compressedSamples) {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  return db.exec("INSERT OR REPLACE INTO run_data (uid, metadata, samples, updated_at) VALUES (?, ?, ?, ?)",
                 {uid, compactJson(metadata), compressedSamples, nowIso()});
}

bool CollabHostStore::runData(QString const &uid, QJsonObject &metadata, QByteArray &compressedSamples) {
  ScopedDb db(COLLAB_HOST_DB);
  ensureSchema(db);
  QSqlQuery q = db.query("SELECT metadata, samples FROM run_data WHERE uid = ?", {uid});
  if (!q.next()) {
    return false;
  }
  metadata          = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
  compressedSamples = q.value(1).toByteArray();
  return true;
}

} // namespace deepvaccontrol::collab
