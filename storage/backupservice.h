#ifndef BACKUPSERVICE_H
#define BACKUPSERVICE_H

#include <QFileInfoList>
#include <QMap>
#include <QStringList>

namespace deepvaccontrol::storage {

/**
 * @brief Local backups of the app's SQLite databases (port of Insight's backup_service).
 * Every data/*.sqlite3 is snapshotted into data/backups/<name>/<name>_<stamp>_<reason>.sqlite3
 * with SQLite's VACUUM INTO (a consistent copy even while the database is open),
 * at most once every N days (Settings > Application) per database unless forced, keeping the newest MAX_BACKUPS_PER_DB.
 */
class BackupService {
public:
  static constexpr int MAX_BACKUPS_PER_DB{14};

  static QStringList databases(); // file names in data/
  // returns the created backup files; failures are reported in errors
  // not forced: a database is backed up when its newest automatic backup is everyDays days old (or it has none)
  static QStringList backupAll(bool force, QStringList *errors = nullptr, int everyDays = 1);
  static QString     backupDatabase(QString const &fileName, QString const &reason, QString *error = nullptr);
  static QMap<QString, QFileInfoList> listBackups(); // db stem -> backups, newest first
  // replaces data/<fileName> with the backup; the current file is kept as a "pre-restore" backup
  static bool restore(QString const &fileName, QString const &backupPath, QString *error = nullptr);
};

} // namespace deepvaccontrol::storage

#endif // BACKUPSERVICE_H
