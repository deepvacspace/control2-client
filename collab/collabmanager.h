#ifndef COLLABMANAGER_H
#define COLLABMANAGER_H

#include <QObject>
#include <QTimer>
#include <functional>

#include "collabclient.h"
#include "collabhoststore.h"
#include "collabsync.h"

namespace deepvaccontrol {

struct ResHolder;

namespace collab {
class CollabServer;
}

/**
 * @brief Local-network sharing between Deepvac Control apps (no accounts, no cloud).
 * One computer hosts (HTTPS server, pinned certificate); others pair with a one-time join code
 * and synchronise. Network work runs in background threads; results come back as signals.
 * State is kept in settings.json ("collab").
 */
class CollabManager : public QObject {
  Q_OBJECT
public:
  static constexpr int AUTO_SYNC_PERIOD_MS{5 * 60 * 1000};

  explicit CollabManager(ResHolder *resHolder, QObject *parent = nullptr);

  [[nodiscard]] bool available() const; // built with OpenSSL

  // host
  bool                  startHost(quint16 port, QString *error = nullptr);
  void                  stopHost();
  [[nodiscard]] bool    isHosting() const;
  [[nodiscard]] quint16 hostPort() const;
  [[nodiscard]] QString hostFingerprint() const;
  QString               newJoinCode();
  QVector<collab::Peer> peers() const;
  void                  removePeer(QString const &keyHash);
  void                  connectToOwnHost(); // the host computer takes part in the sharing too

  // client
  [[nodiscard]] collab::Connection connection() const;
  [[nodiscard]] bool               isConnected() const;
  void                             fetchCertificate(QString const &address);
  void                             pair(QString const &url, QString const &joinCode, QByteArray const &certPem);
  void                             disconnectFromHost();

  void               syncNow();
  [[nodiscard]] bool isBusy() const;
  void               setAutoSync(bool enabled);
  [[nodiscard]] bool autoSync() const;
  [[nodiscard]] QString lastSyncSummary() const;

  void downloadRun(QString const &uid);

signals:
  void stateChanged();
  void certificateFetched(QString const &url, QByteArray const &certPem, QString const &fingerprint, QString const &error);
  void paired(bool ok, QString const &message);
  void syncFinished(bool ok, QString const &summary);
  void downloadFinished(bool ok, QString const &message);
  // local data changed by a sync or download (notes, test profiles, experiments)
  void localDataChanged();

private:
  void        runInBackground(std::function<void()> work, std::function<void()> done);
  void        saveState();
  QString     displayName() const;
  QVariantMap section() const;

  ResHolder            *m_resHolder;
  collab::CollabServer *m_server{nullptr};
  collab::Connection    m_connection;
  QTimer                m_autoSyncTimer;
  int                   m_busy{0};
  QString               m_lastSyncSummary;
};

} // namespace deepvaccontrol

#endif // COLLABMANAGER_H
