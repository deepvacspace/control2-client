#ifndef EXPERIMENTSYNCSERVER_H
#define EXPERIMENTSYNCSERVER_H

#include <QHostAddress>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QTcpServer;
class QTcpSocket;

namespace deepvaccontrol {

class Logger;

// Read-only NDJSON telemetry server consumed by Deepvac Insight's existing
// ChamberConnection client. Only samples belonging to an active AI experiment
// are broadcast. Incoming data is intentionally discarded in this first phase.
class ExperimentSyncServer : public QObject {
  Q_OBJECT
public:
  explicit ExperimentSyncServer(Logger *logger, QObject *parent = nullptr);

  bool listen(QHostAddress const &address = QHostAddress::AnyIPv4, quint16 port = 5555);
  void close();

  QString beginExperiment(QString const &mode);
  void    endExperiment(QString const &runId);
  void    publishSample(QStringList const &names, QVector<float> const &values, qint64 timestampMs);

  [[nodiscard]] bool    isListening() const;
  [[nodiscard]] quint16 serverPort() const;
  [[nodiscard]] QString activeRunId() const;
  [[nodiscard]] QStringList clientAddresses() const;
  [[nodiscard]] QString lastError() const;

signals:
  void listeningChanged(bool listening);
  void clientsChanged();
  void activeRunChanged(QString const &runId);

private:
  QString nextRunId();
  void    acceptPendingConnections();
  void    removeClient(QTcpSocket *socket);

  Logger            *m_logger;
  QTcpServer        *m_server;
  QSet<QTcpSocket *> m_clients;
  QString            m_activeRunId;
  QString            m_activeMode;
  QString            m_lastError;
  quint64            m_sequence{0};
};

} // namespace deepvaccontrol

#endif // EXPERIMENTSYNCSERVER_H
