#include "experimentsyncserver.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <cmath>
#include <utility>

#include "logger.h"

namespace deepvaccontrol {

namespace {
  constexpr char const COUNTER_SETTINGS_GROUP[]{"experiment_sync"};
  constexpr char const COUNTER_SETTINGS_KEY[]{"next_run_counter"};

  QJsonValue jsonNumber(float value) {
    return std::isfinite(value) ? QJsonValue(static_cast<double>(value)) : QJsonValue(QJsonValue::Null);
  }
}

ExperimentSyncServer::ExperimentSyncServer(Logger *logger, QObject *parent)
    : QObject{parent}, m_logger{logger}, m_server{new QTcpServer(this)} {
  connect(m_server, &QTcpServer::newConnection, this, &ExperimentSyncServer::acceptPendingConnections);
}

bool ExperimentSyncServer::listen(QHostAddress const &address, quint16 port) {
  if (m_server->isListening()) {
    m_server->close();
  }

  if (!m_server->listen(address, port)) {
    m_lastError = m_server->errorString();
    m_logger->stream(Logger::Level::ERR) << "Deepvac Insight telemetry server failed to listen on" << address.toString()
                                         << port << m_server->errorString();
    emit listeningChanged(false);
    return false;
  }
  m_lastError.clear();
  emit listeningChanged(true);

  m_logger->stream(Logger::Level::INFO) << "Deepvac Insight telemetry server listening on"
                                        << m_server->serverAddress().toString() << m_server->serverPort();
  return true;
}

void ExperimentSyncServer::close() {
  for (QTcpSocket *client : std::as_const(m_clients)) {
    client->disconnectFromHost();
  }
  m_clients.clear();
  m_server->close();
  m_activeRunId.clear();
  m_activeMode.clear();
  m_sequence = 0;
  emit clientsChanged();
  emit activeRunChanged({});
  emit listeningChanged(false);
}

QString ExperimentSyncServer::beginExperiment(QString const &mode) {
  if (!m_server->isListening()) {
    return {};
  }
  if (!m_activeRunId.isEmpty()) {
    m_logger->stream(Logger::Level::WAR) << "Cannot begin a second synchronized experiment while" << m_activeRunId
                                         << "is active";
    return {};
  }

  m_activeRunId = nextRunId();
  m_activeMode  = mode;
  m_sequence    = 0;
  m_logger->stream(Logger::Level::INFO) << "Experiment sync started:" << m_activeRunId << "mode:" << mode;
  emit activeRunChanged(m_activeRunId);
  return m_activeRunId;
}

void ExperimentSyncServer::endExperiment(QString const &runId) {
  if (runId.isEmpty() || (runId != m_activeRunId)) {
    return;
  }

  m_logger->stream(Logger::Level::INFO) << "Experiment sync finished:" << m_activeRunId << "samples:" << m_sequence;
  m_activeRunId.clear();
  emit activeRunChanged({});
  m_activeMode.clear();
  m_sequence = 0;
}

void ExperimentSyncServer::publishSample(QStringList const &names, QVector<float> const &values, qint64 timestampMs) {
  if (m_activeRunId.isEmpty() || m_clients.isEmpty() || (names.size() != values.size())) {
    return;
  }

  QJsonObject sample;
  sample.insert("run_id", m_activeRunId);
  sample.insert("experiment_mode", m_activeMode);
  sample.insert("sequence", static_cast<qint64>(m_sequence));
  sample.insert("timestamp", static_cast<double>(timestampMs) / 1000.0);

  for (qsizetype i{0}; i < names.size(); ++i) {
    sample.insert(names[i], jsonNumber(values[i]));
  }

  // Insight's established run_samples.csv schema uses kp/ki/kd, while the
  // chamber state list calls the same values temp_kp/temp_ki/temp_kd.
  if (sample.contains("temp_kp")) {
    sample.insert("kp", sample.value("temp_kp"));
  }
  if (sample.contains("temp_ki")) {
    sample.insert("ki", sample.value("temp_ki"));
  }
  if (sample.contains("temp_kd")) {
    sample.insert("kd", sample.value("temp_kd"));
  }

  QByteArray const line = QJsonDocument(sample).toJson(QJsonDocument::Compact) + '\n';
  for (QTcpSocket *client : std::as_const(m_clients)) {
    if (QAbstractSocket::ConnectedState == client->state()) {
      client->write(line);
    }
  }
  ++m_sequence;
}

bool ExperimentSyncServer::isListening() const {
  return m_server->isListening();
}

quint16 ExperimentSyncServer::serverPort() const {
  return m_server->serverPort();
}

QString ExperimentSyncServer::activeRunId() const {
  return m_activeRunId;
}

QString ExperimentSyncServer::nextRunId() {
  QSettings settings;
  settings.beginGroup(COUNTER_SETTINGS_GROUP);
  quint64 const counter = settings.value(COUNTER_SETTINGS_KEY, 0).toULongLong();
  settings.setValue(COUNTER_SETTINGS_KEY, counter + 1);
  settings.endGroup();
  settings.sync();

  if (QSettings::NoError != settings.status()) {
    m_logger->stream(Logger::Level::WAR) << "Could not persist the experiment run counter";
  }

  QString const timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
  return QString("experiment_%1_%2").arg(timestamp).arg(counter, 6, 10, QLatin1Char('0'));
}

void ExperimentSyncServer::acceptPendingConnections() {
  while (m_server->hasPendingConnections()) {
    QTcpSocket *client = m_server->nextPendingConnection();
    if (!client) {
      continue;
    }

    m_clients.insert(client);
    emit clientsChanged();
    m_logger->stream(Logger::Level::INFO)
      << "Deepvac Insight client connected:" << client->peerAddress().toString() << client->peerPort();

    // This phase exposes telemetry only. Discard commands sent by Insight's
    // optional controller UI so they can never reach the chamber accidentally.
    connect(client, &QTcpSocket::readyRead, client, [this, client]() {
      QByteArray const ignored = client->readAll();
      if (!ignored.isEmpty()) {
        m_logger->stream(Logger::Level::WAR) << "Ignored data received from Deepvac Insight (read-only connection)";
      }
    });
    connect(client, &QTcpSocket::disconnected, this, [this, client]() {
      removeClient(client);
    });
  }
}

void ExperimentSyncServer::removeClient(QTcpSocket *socket) {
  if (!socket) {
    return;
  }
  m_logger->stream(Logger::Level::INFO) << "Deepvac Insight client disconnected:" << socket->peerAddress().toString();
  m_clients.remove(socket);
  socket->deleteLater();
  emit clientsChanged();
}

QStringList ExperimentSyncServer::clientAddresses() const {
  QStringList addresses;
  for (QTcpSocket const *client : m_clients) {
    addresses.push_back(QStringLiteral("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort()));
  }
  addresses.sort();
  return addresses;
}

QString ExperimentSyncServer::lastError() const {
  return m_lastError;
}

} // namespace deepvaccontrol
