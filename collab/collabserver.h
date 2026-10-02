#ifndef COLLABSERVER_H
#define COLLABSERVER_H

#include <QByteArray>
#include <QDeadlineTimer>
#include <QHash>
#include <QObject>

class QSslServer;
class QSslSocket;

namespace deepvaccontrol::collab {

/**
 * @brief Local-network sharing host for Deepvac Control apps:
 * HTTPS with a self-signed certificate that peers pin, pairing with a one-time join code,
 * and Ed25519-signed requests from paired devices to share annotations, variable rules,
 * test profiles, the experiment list and full experiments.
 */
class CollabServer : public QObject {
  Q_OBJECT
public:
  static constexpr quint16 DEFAULT_PORT{8765};

  explicit CollabServer(QObject *parent = nullptr);
  ~CollabServer() override;

  bool                  start(quint16 port, QString *error = nullptr);
  void                  stop();
  [[nodiscard]] bool    isRunning() const;
  [[nodiscard]] quint16 port() const;
  [[nodiscard]] QByteArray certPem() const;

  // new one-time code (valid 10 minutes, 5 attempts), formatted "ABCD-EFGH"
  QString newJoinCode();
  [[nodiscard]] bool joinCodeActive() const;

signals:
  void peersChanged();
  void itemsChanged();

private:
  struct Request {
    QByteArray buffer;
    bool       headersDone{false};
    QByteArray method;
    QByteArray path;
    QHash<QByteArray, QByteArray> headers; // lower-case names
    qsizetype  bodyLength{0};
    qsizetype  headerLength{0};
  };

  void onNewConnection();
  void onReadyRead(QSslSocket *socket);
  void handle(QSslSocket *socket, Request const &request, QByteArray const &body);
  void send(QSslSocket *socket, int status, QByteArray const &json);
  bool consumeJoinCode(QString const &code);

  QSslServer                 *m_server;
  QByteArray                  m_certPem;
  QHash<QSslSocket *, Request> m_requests;
  QString                     m_joinCode;
  QDeadlineTimer              m_joinCodeDeadline;
  int                         m_joinCodeFailures{0};
};

} // namespace deepvaccontrol::collab

#endif // COLLABSERVER_H
