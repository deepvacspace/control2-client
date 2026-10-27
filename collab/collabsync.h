#ifndef COLLABSYNC_H
#define COLLABSYNC_H

#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVector>

#include "collabclient.h"

namespace deepvaccontrol::collab {

struct KindResult {
  int pushed{0};
  int pulled{0};
  int removed{0};
  int skipped{0};
};

struct SyncResult {
  QMap<QString, KindResult> kinds;
  QStringList               errors;

  [[nodiscard]] QString summary() const;
};

struct SharedRun {
  QString     uid;
  QJsonObject metadata;
  bool        hasData{false}; // full experiment available for download
  bool        local{false};   // already in this computer's database
};

/**
 * @brief Synchronises local data with the connected host: annotations, variable rules,
 * test profiles, the experiment list and full experiment data. Synchronous: run from a worker thread.
 */
class CollabSync {
public:
  static SyncResult sync(Connection const &connection, QString const &userName);

  // runs shared through the host (cached in deepvac_shared_runs.sqlite3)
  static QVector<SharedRun> sharedRuns();
  // downloads a shared experiment into the local experiment database; returns its key
  static QString downloadRun(Connection const &connection, QString const &uid, QString *error = nullptr);
};

}

#endif // COLLABSYNC_H
