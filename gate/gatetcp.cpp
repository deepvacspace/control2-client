#include "gatetcp.h"

#include <QAbstractSocket>
#include <QDataStream>
#include <QDateTime>

#include "messagedialog.h"
namespace control2 {
GateTcp::GateTcp(
  Logger *logger, QHostAddress serverIp4Address, quint16 port, ConnectionDialog *connectionDialog, QObject *parent)
    : QObject{parent}
    , m_logger{logger}
    , m_socket{new QTcpSocket(this)}
    , m_streambuffer{logger, this}
    , m_serverIp4Address{serverIp4Address}
    , m_port{port}
    , m_state{ConnectionState::DUMMY} // , m_prevSocketState{QAbstractSocket::SocketState::UnconnectedState}
    , m_contConnectingTryCount{0}
    , m_successConnectionCount{0}
    , m_notifyTimeout{true}
    , m_connectionDialog{connectionDialog}
    , m_sendPacketCount{0} {
  // Signal
  connect(m_socket, &QTcpSocket::errorOccurred, this, &GateTcp::onError);
  connect(m_socket, &QTcpSocket::stateChanged, this, &GateTcp::onStateChanged);
  connect(m_socket, &QTcpSocket::readyRead, this, &GateTcp::onReadReady);

  // Setup timer
  m_connectingTimer = new QTimer();
  m_connectingTimer->setInterval(10000);
  m_connectingTimer->setSingleShot(true); // repeat starting manualy
  connect(m_connectingTimer, &QTimer::timeout, this, &GateTcp::connectingTimeout);

  m_keepAliveTimer = new QTimer();
  m_keepAliveTimer->setInterval(5000);
  m_keepAliveTimer->setSingleShot(false);
  connect(m_keepAliveTimer, &QTimer::timeout, this, &GateTcp::sendKeepAlive);

  connect(m_connectionDialog, &QDialog::finished, this, [this](int result) {
    switch (result) {
      case 1: // hide
        m_notifyTimeout = false;
        emit showRedFlag(false);
        break;

      case 2: // offline
        break;

      case 3: // close
        break;

      case 0:
      default:
        break;
    }
  });
}

void GateTcp::connectToServer() {
  if (QAbstractSocket::UnconnectedState == m_socket->state()) {
    ++m_contConnectingTryCount;

    QString tryConnectMsg{
      (m_contConnectingTryCount > 1
         ? QString("Attempting to connect %1:%2 (try number: %3) ...")
             .arg(m_serverIp4Address.toString())
             .arg(m_port)
             .arg(m_contConnectingTryCount)
         : QString("Attempting to connect %1:%2 ...").arg(m_serverIp4Address.toString()).arg(m_port))};
    m_logger->stream(Logger::Level::INFO) << tryConnectMsg;

    m_connectingTimer->start();
    m_socket->connectToHost(m_serverIp4Address, m_port, QIODeviceBase::ReadWrite);
    setState(ConnectionState::CONNECTING);
  }
}

void GateTcp::onStateChanged(QAbstractSocket::SocketState socketState) {
  QAbstractSocket::SocketError const socketErr{m_socket->error()};
  QString const                      errMsg{
    (QAbstractSocket::SocketError::UnknownSocketError == socketErr) ? "" : "| error =" + m_socket->errorString()};

  m_logger->stream(Logger::Level::DEB) << "New socket state =" << socketState << errMsg;
  switch (socketState) {
    case QAbstractSocket::SocketState::ConnectedState:
      onConnected();
      break;
    case QAbstractSocket::UnconnectedState:
      if (ConnectionState::ABORTING != m_state
          && (QAbstractSocket::SocketError::NetworkError
              != socketErr)) { // aborting connection by timeout try reconnect by itself
        onDisconnected();
      }
      break;
    default:
      break;
  }

  // m_prevSocketState = socketState;
}

void GateTcp::onConnected() {
  setState(ConnectionState::CONNECTED);
}

void GateTcp::onDisconnected() {
  m_logger->stream(Logger::Level::WAR)
    << QString("Disconnected from server %1 somehow.").arg(m_serverIp4Address.toString());

  setState(ConnectionState::ERROR_CON);
}

void GateTcp::onError(QAbstractSocket::SocketError socketError) {
  if (QAbstractSocket::UnconnectedState == m_socket->state()) {
    return; // onStateChanged process this
  }

  if (QAbstractSocket::UnknownSocketError == socketError) {
    return;
  }

  m_logger->stream(Logger::Level::WAR) << "Connection error:" << socketError << m_socket->errorString();

  setState(ConnectionState::ERROR_CON);
}

void GateTcp::connectingTimeout() {
  m_logger->stream(Logger::Level::WAR) << QString("Timeout exceeded (%1s). Closing socket and restarting connection...")
                                            .arg(m_connectingTimer->interval() / 1000);
  setState(ConnectionState::ABORTING);

  m_socket->abort();

  setState(ConnectionState::ERROR_CON);
}

void GateTcp::setState(ConnectionState const newState) {
  if (newState == m_state) {
    return;
  }

  m_state = newState;
  auto stateString
    = QStringList({"IDLE", "CONNECTING", "CONNECTED", "ABORTING", "ERROR", "DUMMY"}).at(static_cast<int>(newState));
  m_logger->stream(Logger::Level::DEB) << stateString;

  switch (newState) {
    case ConnectionState::IDLE:
      break;

    case ConnectionState::CONNECTING:
      m_connectionDialog->updateContent(m_contConnectingTryCount, m_successConnectionCount);
      if (((m_contConnectingTryCount > 1) || (m_successConnectionCount > 0)) && m_notifyTimeout) {
        m_connectionDialog->show();
        emit showRedFlag(true);
      }
      break;

    case ConnectionState::CONNECTED:
      m_connectingTimer->stop();
      m_keepAliveTimer->start();

      m_connectionDialog->reset();
      m_notifyTimeout = true;
      emit showRedFlag(false);
      if ((m_contConnectingTryCount > 1) || (m_successConnectionCount > 0)) {
        auto *msgDialog = new MessageDialog(
          tr("Уведомление"), QString(tr("Соединение с %1:%2 установлено")).arg(m_serverIp4Address.toString()).arg(m_port));
        msgDialog->open(3000);
      }

      m_contConnectingTryCount = 0;
      ++m_successConnectionCount;
      emit connected();
      break;

    case ConnectionState::ABORTING:
      break;

    case ConnectionState::ERROR_CON:
      setState(ConnectionState::IDLE); // important for state-machine work
      m_keepAliveTimer->stop();
      connectToServer();
      emit disconnected();
      break;

    case ConnectionState::DUMMY:
    default:
      break;
  }
}

void GateTcp::sendData(QByteArray const &data) {
  if (QAbstractSocket::ConnectedState == m_socket->state()) {
    QByteArray const packet = StreamBuffer::makePackage(data);

    m_socket->write(packet);

    int const maxDisplay{20};
    if (packet.size() < maxDisplay) {
      m_logger->stream(Logger::Level::INFO) << QString("(%1)").arg(packet.size()) << packet.toHex(' ');
    } else {
      m_logger->stream(Logger::Level::INFO)
        << QString("(%1)").arg(packet.size()) << packet.first(maxDisplay).toHex(' ') << "...";
    }
  } else {
    m_logger->stream(Logger::Level::ERR) << "Попытка записи в неподключенный сокет";
  }
}

void GateTcp::sendGeneral(OutcomeMessageCode cmdCode, QByteArray data) {
  data.push_front(static_cast<char>(cmdCode));

  sendData(data);
}

void GateTcp::sendPair(QString const &text, float value) {
  QByteArray data = utils::makePascalStringPair(text, value);
  data.push_front(static_cast<char>(OutcomeMessageCode::PAIR));

  sendData(data);
}

void GateTcp::sendKeepAlive() {
  sendTextCmd("ka");
}

void GateTcp::sendTextCmd(QString const &cmd) {
  QByteArray data = utils::makePascalString(cmd);
  // Add code for 'text command' type of message
  data.push_front(static_cast<char>(OutcomeMessageCode::TEXT_CMD));

  sendData(data);
}

void GateTcp::sendSettingMap(QMap<QString, float> const &map) {
  m_logger->stream(Logger::Level::DEB) << QString("(%1)").arg(map.size()) << map;

  QByteArray data;

  // Add code for setting map
  data.push_back(static_cast<char>(OutcomeMessageCode::SETTINGS_MAP));

  data.push_back(static_cast<char>(map.size()));

  for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
    data.append(utils::makePascalStringPair(it.key(), it.value()));
  }
  sendData(data);
}

void GateTcp::onReadReady() {
  QByteArray data{m_socket->readAll()};

  int const maxDisplay{20};
  if (data.size() < maxDisplay) {
    m_logger->stream(Logger::Level::INFO) << QString("(%1)").arg(data.size()) << data.toHex(' ');
  } else {
    m_logger->stream(Logger::Level::INFO)
      << QString("(%1)").arg(data.size()) << data.first(maxDisplay).toHex(' ') << "...";
  }

  m_streambuffer.addData(data);

  // Incoming data could contain not just one message, so take all of them
  QByteArray nextPacket{m_streambuffer.getPackage()};
  while (!nextPacket.isEmpty()) {
    parsePlcMessage(nextPacket);
    nextPacket = m_streambuffer.getPackage();
  }
}

void GateTcp::parsePlcMessage(QByteArray const &data) {
  // if (data.size() < 2) {
  //   m_logger->stream(Logger::Level::WAR) << "Incoming msg with size < 2";
  //   return;
  // }

  // Parsing command code
  IncomeMessageCode const cmdCode{static_cast<IncomeMessageCode>(data.front())};
  auto const              cmdCodeSize{sizeof(IncomeMessageCode)};

  switch (cmdCode) {
    case IncomeMessageCode::STATE_VALUES:
      m_logger->stream(Logger::Level::DEB) << "Get state values";
      parseStateValuesList(data.sliced(cmdCodeSize)); // drop 1-byte code from data
      break;
    case IncomeMessageCode::STATE_NAMES:
      m_logger->stream(Logger::Level::DEB) << "Get state names";
      parseStateNamesList(data.sliced(cmdCodeSize)); // drop 1-byte code from data
      break;
    case IncomeMessageCode::SETTINGS_MAP:
      m_logger->stream(Logger::Level::DEB) << "Get settings map";
      parseSettingsMap(data.sliced(cmdCodeSize)); // drop 1-byte code from data
      break;
    case IncomeMessageCode::ALARM:
      m_logger->stream(Logger::Level::DEB) << "Get alarm code";
      parseAlarm(data.sliced(cmdCodeSize)); // drop 1-byte code from data
      break;
    case IncomeMessageCode::JOB_FINISHED:
      m_logger->stream(Logger::Level::DEB) << "Get job finished";
      emit jobFinishedReceived();
      break;
    case IncomeMessageCode::USER_MSG:
      m_logger->stream(Logger::Level::DEB) << "Get message from user";
      parseUserMsg(data.sliced(cmdCodeSize)); // drop 1-byte code from data
      break;
    default:
      break;
  }
}

bool GateTcp::parseStateNamesList(QByteArray data) {
  if (data.size() < 1) {
    m_logger->stream(Logger::Level::WAR) << "Incoming state names list with data size == 0";
    return false;
  }

  // Parsing number of state names
  quint8 const count{static_cast<quint8>(data.front())};
  data.remove(0, 1); // drop count from data

  QStringList states;
  while (!data.isEmpty()) {
    QString const stateName{utils::fromPascalString(data)};
    if (!stateName.isEmpty()) {
      states << stateName;
    }
  }

  m_logger->stream(Logger::Level::DEB) << states;

  Q_ASSERT(states.size() == count);

  emit stateListReceived(states);

  return states.size() == count;
}

bool GateTcp::parseStateValuesList(QByteArray data) {
  if (data.size() < 1) {
    m_logger->stream(Logger::Level::WAR) << "Incoming state values list with data size == 0";
    return false;
  }

  // Parsing number of state values
  quint8 const count{static_cast<quint8>(data.front())};
  data.remove(0, 1); // drop count from data

  QVector<float> stateValues;
  QDataStream    valueDataStream(data);
  valueDataStream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
  valueDataStream.setFloatingPointPrecision(QDataStream::FloatingPointPrecision::SinglePrecision);
  float tmpValue{};
  for (int i{0U}; i < count; ++i) {
    valueDataStream >> tmpValue;
    if (QDataStream::Ok != valueDataStream.status()) {
      break;
    }
    stateValues.push_back(tmpValue);
  }

  m_logger->stream(Logger::Level::DEB) << stateValues;

  Q_ASSERT(stateValues.size() == count);

  emit stateValuesListReceived(stateValues);

  return stateValues.size() == count;
}

bool GateTcp::parseSettingsMap(QByteArray data) {
  if (data.size() < 1) {
    m_logger->stream(Logger::Level::WAR) << "Incoming settings map list with data size == 0";
    return false;
  }

  // Parsing number of settings
  quint8 const count{static_cast<quint8>(data.front())};
  data.remove(0, 1); // drop count from data

  if (0 == count) {
    m_logger->stream(Logger::Level::WAR) << "Settings map has 0 elements";
    emit settingsReceived({});
    return true;
  }

  QString              settingName;
  float                settingValue{0.0f};
  QMap<QString, float> settingsMap;
  for (int i{0}; i < count; ++i) {
    settingName = utils::fromPascalString(data);

    if (settingName.isEmpty()) {
      m_logger->stream(Logger::Level::WAR) << "Setting name is empty";
      return false;
    }

    if (data.size() < 4) {
      m_logger->stream(Logger::Level::WAR) << "Raw value data size < 4";
      return false;
    }

    auto const valueData = data.first(4);
    data.remove(0, 4); // drop value from data
    QDataStream valueDataStream(valueData);
    valueDataStream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
    valueDataStream.setFloatingPointPrecision(QDataStream::FloatingPointPrecision::SinglePrecision);
    valueDataStream >> settingValue;

    settingsMap[settingName] = settingValue;
  }

  Q_ASSERT(data.isEmpty());

  emit settingsReceived(settingsMap);

  return settingsMap.size() == count;
}

bool GateTcp::parseAlarm(QByteArray data) {
  if (data.size() != 4) {
    m_logger->stream(Logger::Level::ERR) << "Incoming alarm with data size != 4";
    return false;
  }

  quint32     alarmCode;
  QDataStream valueDataStream(data);
  valueDataStream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
  valueDataStream >> alarmCode;

  emit alarmReceived(alarmCode);
  return true;
}

bool GateTcp::parseUserMsg(QByteArray data) {
  if (data.size() < 1) {
    m_logger->stream(Logger::Level::WAR) << "Incoming user message with zero size";
    return false;
  }

  QString const userMsg{utils::fromPascalString(data)};
  // QString::fromLocal8Bit(rawData.data(), rawData.size()), ack);
  emit          userMsgReceived(userMsg);
  return true;
}

} // namespace control2
