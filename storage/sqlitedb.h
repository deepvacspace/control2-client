#ifndef SQLITEDB_H
#define SQLITEDB_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVariant>

namespace deepvaccontrol::storage {

// Databases shared with Deepvac Insight: same file names and schemas, in the app's data/ folder.
inline QString const RUNS_DB{"deepvac_runs.sqlite3"};
inline QString const TEST_PROFILES_DB{"deepvac_test_profiles.sqlite3"};
inline QString const ANNOTATIONS_DB{"deepvac_annotations.sqlite3"};
inline QString const COLLAB_HOST_DB{"deepvac_collab_host.sqlite3"};
inline QString const SHARED_RUNS_DB{"deepvac_shared_runs.sqlite3"};

QString dataDir();
QString dbPath(QString const &fileName);
QString backupsDir();

// UTC timestamp formatted like Python's datetime.isoformat() (what Insight writes).
QString nowIso();

/**
 * @brief One short-lived SQLite connection (like Insight's connect_*() helpers).
 * Every call opens its own uniquely named connection, so it can be used from any thread
 * and a database file can be replaced (restore) while the app runs.
 */
class ScopedDb {
public:
  explicit ScopedDb(QString const &fileName);
  ~ScopedDb();
  ScopedDb(ScopedDb const &)            = delete;
  ScopedDb &operator=(ScopedDb const &) = delete;

  [[nodiscard]] bool          isOpen() const;
  [[nodiscard]] QSqlDatabase &db();
  [[nodiscard]] QString       lastError() const;

  // Runs a statement without results; remembers the error text on failure.
  bool exec(QString const &sql, QVariantList const &bindings = {});
  // Prepared query, already executed; check query.lastError() / isActive().
  QSqlQuery query(QString const &sql, QVariantList const &bindings = {});
  // Column names of a table (PRAGMA table_info).
  QStringList columns(QString const &table);

  bool transaction();
  bool commit();
  void rollback();

private:
  QString      m_connectionName;
  QSqlDatabase m_db;
  QString      m_error;
};

}

#endif // SQLITEDB_H
