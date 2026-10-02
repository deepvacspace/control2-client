#include "sqlitedb.h"

#include <QAtomicInteger>
#include <QDateTime>
#include <QDir>
#include <QSqlError>
#include <QThread>

#include "constants.h"

namespace deepvaccontrol::storage {

namespace {
QAtomicInteger<quint64> g_connectionCounter{0};
}

QString dataDir() {
  return QDir(deepvaccontrol::INTERVALS_DIR).absolutePath();
}

QString dbPath(QString const &fileName) {
  return QDir(dataDir()).filePath(fileName);
}

QString backupsDir() {
  return QDir(dataDir()).filePath("backups");
}

QString nowIso() {
  return QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ss.zzz000") + "+00:00";
}

ScopedDb::ScopedDb(QString const &fileName)
    : m_connectionName{QStringLiteral("deepvac_%1_%2")
                         .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
                         .arg(++g_connectionCounter)} {
  QDir().mkpath(dataDir());
  m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
  m_db.setDatabaseName(QDir::isAbsolutePath(fileName) ? fileName : dbPath(fileName));
  m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
  if (!m_db.open()) {
    m_error = m_db.lastError().text();
    return;
  }
  // same pragmas as Insight
  exec("PRAGMA journal_mode=WAL");
  exec("PRAGMA foreign_keys=ON");
}

ScopedDb::~ScopedDb() {
  if (m_db.isOpen()) {
    m_db.close();
  }
  m_db = QSqlDatabase();
  QSqlDatabase::removeDatabase(m_connectionName);
}

bool ScopedDb::isOpen() const {
  return m_db.isOpen();
}

QSqlDatabase &ScopedDb::db() {
  return m_db;
}

QString ScopedDb::lastError() const {
  return m_error;
}

bool ScopedDb::exec(QString const &sql, QVariantList const &bindings) {
  QSqlQuery q = query(sql, bindings);
  return !q.lastError().isValid();
}

QSqlQuery ScopedDb::query(QString const &sql, QVariantList const &bindings) {
  QSqlQuery q(m_db);
  q.setForwardOnly(true);
  if (!q.prepare(sql)) {
    m_error = q.lastError().text();
    return q;
  }
  for (QVariant const &value : bindings) {
    q.addBindValue(value);
  }
  if (!q.exec()) {
    m_error = q.lastError().text();
  }
  return q;
}

QStringList ScopedDb::columns(QString const &table) {
  QStringList result;
  QSqlQuery   q = query(QStringLiteral("PRAGMA table_info(%1)").arg(table));
  while (q.next()) {
    result.push_back(q.value("name").toString());
  }
  return result;
}

bool ScopedDb::transaction() {
  return m_db.transaction();
}

bool ScopedDb::commit() {
  if (!m_db.commit()) {
    m_error = m_db.lastError().text();
    return false;
  }
  return true;
}

void ScopedDb::rollback() {
  m_db.rollback();
}

} // namespace deepvaccontrol::storage
