#include "collabmanager.h"

#include <QDateTime>
#include <QHostInfo>
#include <QSslSocket>
#include <QThread>

#include "collabcrypto.h"
#include "collabserver.h"
#include "experimentrecorder.h"
#include "resholder.h"

namespace deepvaccontrol {

using namespace collab;

namespace {
QString const SECTION{"collab"};
}

CollabManager::CollabManager(ResHolder *resHolder, QObject *parent) : QObject{parent}, m_resHolder{resHolder} {
  if (!available()) {
    return;
  }
  // QSslServer/QSslSocket need the OpenSSL backend for the EC host key (shipped next to the exe)
  if (QSslSocket::availableBackends().contains("openssl")) {
    QSslSocket::setActiveBackend("openssl");
  }

  m_server = new CollabServer(this);
  connect(m_server, &CollabServer::peersChanged, this, &CollabManager::stateChanged);

  QVariantMap const state = section();
  m_connection.url      = state.value("url").toString();
  m_connection.hostName = state.value("host_name").toString();
  m_connection.certPem  = state.value("cert_pem").toString().toLatin1();

  m_autoSyncTimer.setInterval(AUTO_SYNC_PERIOD_MS);
  connect(&m_autoSyncTimer, &QTimer::timeout, this, [this] {
    if (isConnected() && !isBusy()) {
      syncNow();
    }
  });
  if (state.value("auto_sync", false).toBool()) {
    m_autoSyncTimer.start();
  }

  if (state.value("host_enabled", false).toBool()) {
    startHost(static_cast<quint16>(state.value("host_port", CollabServer::DEFAULT_PORT).toUInt()));
  }
}

bool CollabManager::available() const {
  return CollabCrypto::available();
}

QVariantMap CollabManager::section() const {
  return m_resHolder->settings->settingsMap().value(SECTION).toMap();
}

void CollabManager::saveState() {
  QVariantMap state     = section();
  state["host_enabled"] = isHosting();
  if (isHosting()) {
    state["host_port"] = hostPort();
  }
  state["url"]       = m_connection.url;
  state["host_name"] = m_connection.hostName;
  state["cert_pem"]  = QString::fromLatin1(m_connection.certPem);
  state["auto_sync"] = m_autoSyncTimer.isActive();
  m_resHolder->settings->setSection(SECTION, state);
  emit stateChanged();
}

QString CollabManager::displayName() const {
  QString const chamber = m_resHolder->recorder ? m_resHolder->recorder->chamberName() : QString{};
  return chamber.isEmpty() ? QHostInfo::localHostName() : QStringLiteral("%1 (%2)").arg(QHostInfo::localHostName(), chamber);
}

bool CollabManager::startHost(quint16 port, QString *error) {
  if (!m_server) {
    if (error) {
      *error = tr("This build has no OpenSSL support");
    }
    return false;
  }
  bool const ok = m_server->start(port, error);
  saveState();
  return ok;
}

void CollabManager::stopHost() {
  if (m_server) {
    m_server->stop();
  }
  saveState();
}

bool CollabManager::isHosting() const {
  return m_server && m_server->isRunning();
}

quint16 CollabManager::hostPort() const {
  return m_server ? m_server->port() : 0;
}

QString CollabManager::hostFingerprint() const {
  return (m_server && !m_server->certPem().isEmpty()) ? CollabCrypto::fingerprint(m_server->certPem()) : QString{};
}

QString CollabManager::newJoinCode() {
  return m_server ? m_server->newJoinCode() : QString{};
}

QVector<Peer> CollabManager::peers() const {
  return CollabHostStore::peers();
}

void CollabManager::removePeer(QString const &keyHash) {
  CollabHostStore::removePeer(keyHash);
  emit stateChanged();
}

void CollabManager::connectToOwnHost() {
  if (!isHosting()) {
    return;
  }
  // register this device on its own host and connect over loopback
  QByteArray privateKey;
  QByteArray publicKey;
  if (CollabCrypto::loadOrCreateDeviceKey(privateKey, publicKey)) {
    CollabHostStore::registerPeer(publicKey, displayName());
  }
  m_connection = Connection{QStringLiteral("https://127.0.0.1:%1").arg(hostPort()), QHostInfo::localHostName(), m_server->certPem()};
  saveState();
}

Connection CollabManager::connection() const {
  return m_connection;
}

bool CollabManager::isConnected() const {
  return m_connection.isValid();
}

void CollabManager::runInBackground(std::function<void()> work, std::function<void()> done) {
  ++m_busy;
  emit stateChanged();
  QThread *thread = QThread::create(std::move(work));
  connect(thread, &QThread::finished, this, [this, thread, done = std::move(done)] {
    thread->deleteLater();
    --m_busy;
    done();
    emit stateChanged();
  });
  thread->start(QThread::LowPriority);
}

bool CollabManager::isBusy() const {
  return m_busy > 0;
}

void CollabManager::fetchCertificate(QString const &address) {
  QString error;
  QString const url = CollabClient::normalizeHostUrl(address, &error);
  if (url.isEmpty()) {
    emit certificateFetched({}, {}, {}, error);
    return;
  }
  auto result = std::make_shared<QPair<QByteArray, QString>>();
  runInBackground(
    [url, result] {
      result->first = CollabClient::fetchCertificate(url, &result->second);
    },
    [this, url, result] {
      emit certificateFetched(url, result->first, CollabCrypto::fingerprint(result->first), result->second);
    });
}

void CollabManager::pair(QString const &url, QString const &joinCode, QByteArray const &certPem) {
  auto          result = std::make_shared<QPair<QString, QString>>(); // host name, error
  QString const name   = displayName();
  runInBackground(
    [url, joinCode, certPem, name, result] {
      result->first = CollabClient::pair(url, joinCode, name, certPem, &result->second);
    },
    [this, url, certPem, result] {
      if (result->first.isEmpty()) {
        emit paired(false, result->second);
        return;
      }
      m_connection = Connection{url, result->first, certPem};
      saveState();
      emit paired(true, tr("Paired with %1").arg(result->first));
      syncNow();
    });
}

void CollabManager::disconnectFromHost() {
  m_connection = Connection{};
  saveState();
}

void CollabManager::syncNow() {
  if (!isConnected()) {
    emit syncFinished(false, tr("Not connected to a host."));
    return;
  }
  auto          result     = std::make_shared<SyncResult>();
  Connection    connection = m_connection;
  QString const userName   = m_resHolder->users->getCurrentUserName();
  runInBackground(
    [connection, userName, result] {
      *result = CollabSync::sync(connection, userName);
    },
    [this, result] {
      m_lastSyncSummary = tr("%1 — %2").arg(QDateTime::currentDateTime().toString("HH:mm"), result->summary());
      emit localDataChanged();
      emit syncFinished(result->errors.isEmpty(), result->summary());
    });
}

void CollabManager::setAutoSync(bool enabled) {
  if (enabled) {
    m_autoSyncTimer.start();
  } else {
    m_autoSyncTimer.stop();
  }
  saveState();
}

bool CollabManager::autoSync() const {
  return m_autoSyncTimer.isActive();
}

QString CollabManager::lastSyncSummary() const {
  return m_lastSyncSummary;
}

void CollabManager::downloadRun(QString const &uid) {
  auto       result     = std::make_shared<QPair<QString, QString>>(); // key, error
  Connection connection = m_connection;
  runInBackground(
    [connection, uid, result] {
      result->first = CollabSync::downloadRun(connection, uid, &result->second);
    },
    [this, result] {
      if (result->first.isEmpty()) {
        emit downloadFinished(false, result->second);
      } else {
        emit localDataChanged();
        emit downloadFinished(true, tr("Saved in the experiment database."));
      }
    });
}

} // namespace deepvaccontrol
