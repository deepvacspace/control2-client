#ifndef COLLABCLIENT_H
#define COLLABCLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace deepvaccontrol::collab {

struct Connection {
  QString    url;      // https://host:port
  QString    hostName; // as reported by the host when pairing
  QByteArray certPem;  // pinned host certificate

  [[nodiscard]] bool isValid() const {
    return !url.isEmpty() && !certPem.isEmpty();
  }
};

/**
 * @brief Client side of the local-network sharing:
 * pairing with a host and making Ed25519-signed requests over a TLS connection that only
 * trusts the pinned host certificate. Calls are synchronous: use it from a worker thread.
 */
class CollabClient {
public:
  explicit CollabClient(Connection connection);

  static QString    normalizeHostUrl(QString const &text, QString *error = nullptr);
  // the certificate a host presents, without trusting it (to show its fingerprint)
  static QByteArray fetchCertificate(QString const &url, QString *error = nullptr);
  // returns the host name, empty on error
  static QString pair(QString const &url, QString const &joinCode, QString const &displayName, QByteArray const &certPem,
                      QString *error = nullptr);

  // signed JSON request; status is the HTTP status (0 when the host is unreachable)
  bool request(QByteArray const &method, QString const &path, QJsonObject const *payload, QJsonObject &response,
               QString *error = nullptr, int *status = nullptr, int timeoutMs = 30'000);

  [[nodiscard]] Connection const &connection() const;

private:
  static bool send(QByteArray const &method, QString const &url, QByteArray const &body, QByteArray const &certPem,
                   QList<QPair<QByteArray, QByteArray>> const &headers, QJsonObject &response, QString *error, int *status,
                   int timeoutMs);

  Connection m_connection;
  QByteArray m_privateKey;
  QByteArray m_publicKey;
};

inline Connection const &CollabClient::connection() const {
  return m_connection;
}

}

#endif // COLLABCLIENT_H
