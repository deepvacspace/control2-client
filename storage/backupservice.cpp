#include "backupservice.h"

#include <QDate>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlError>

#include "sqlitedb.h"

namespace deepvaccontrol::storage {

namespace {
QString stem(QString const &fileName) {
  return QFileInfo(fileName).completeBaseName();
}

QString stamp(QString const &format) {
  return QDateTime::currentDateTimeUtc().toString(format);
}

void prune(QDir const &dir) {
  QFileInfoList const backups = dir.entryInfoList({"*.sqlite3"}, QDir::Files, QDir::Time); // newest first
  for (qsizetype i{BackupService::MAX_BACKUPS_PER_DB}; i < backups.size(); ++i) {
    QFile::remove(backups[i].absoluteFilePath());
  }
}
}

QStringList BackupService::databases() {
  return QDir(dataDir()).entryList({"*.sqlite3"}, QDir::Files, QDir::Name);
}

QString BackupService::backupDatabase(QString const &fileName, QString const &reason, QString *error) {
  QDir const dir(QDir(backupsDir()).filePath(stem(fileName)));
  QDir().mkpath(dir.absolutePath());
  QString const target = dir.filePath(QStringLiteral("%1_%2_%3.sqlite3").arg(stem(fileName), stamp("yyyyMMdd_HHmmss"), reason));

  ScopedDb db(fileName);
  if (!db.isOpen() || !db.exec("VACUUM INTO ?", {QDir::toNativeSeparators(target)})) {
    if (error) {
      *error = QStringLiteral("%1: %2").arg(fileName, db.lastError());
    }
    QFile::remove(target);
    return {};
  }
  prune(dir);
  return target;
}

QStringList BackupService::backupAll(bool force, QStringList *errors, int everyDays) {
  QStringList   created;
  // a backup made the same calendar day counts as fresh; older ones by whole days since
  QDate const   today = QDate::currentDate();
  for (QString const &fileName : databases()) {
    QDir const dir(QDir(backupsDir()).filePath(stem(fileName)));
    if (!force) {
      QFileInfoList const automatic = dir.entryInfoList({QStringLiteral("%1_*_daily.sqlite3").arg(stem(fileName))}, QDir::Files, QDir::Time);
      if (!automatic.isEmpty() && (automatic.first().lastModified().date().daysTo(today) < std::max(1, everyDays))) {
        continue;
      }
    }
    QString error;
    QString const path = backupDatabase(fileName, force ? QStringLiteral("manual") : QStringLiteral("daily"), &error);
    if (path.isEmpty()) {
      if (errors) {
        errors->push_back(error);
      }
    } else {
      created.push_back(path);
    }
  }
  return created;
}

QMap<QString, QFileInfoList> BackupService::listBackups() {
  QMap<QString, QFileInfoList> result;
  QDir const                   root(backupsDir());
  for (QString const &sub : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    result.insert(sub, QDir(root.filePath(sub)).entryInfoList({"*.sqlite3"}, QDir::Files, QDir::Time));
  }
  return result;
}

bool BackupService::restore(QString const &fileName, QString const &backupPath, QString *error) {
  QString const target = dbPath(fileName);
  if (QFile::exists(target)) {
    // raw copy on purpose: the live file may be the damaged one (same as Insight)
    QDir const dir(QDir(backupsDir()).filePath(stem(fileName)));
    QDir().mkpath(dir.absolutePath());
    QFile::copy(target, dir.filePath(QStringLiteral("%1_%2_pre-restore.sqlite3").arg(stem(fileName), stamp("yyyyMMdd_HHmmss"))));
    QFile::remove(target);
  }
  if (!QFile::copy(backupPath, target)) {
    if (error) {
      *error = QStringLiteral("Could not copy %1").arg(backupPath);
    }
    return false;
  }
  QFile(target).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
  for (QString const &suffix : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
    QFile::remove(target + suffix);
  }
  return true;
}

}
