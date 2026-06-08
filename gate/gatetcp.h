#ifndef GATETCP_H
#define GATETCP_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include "connectiondialog.h"
#include "logger.h"
#include "streambuffer.h"

namespace control2 {

class GateTcp : public QObject {
  Q_OBJECT
public:
  enum class ConnectionState {
    IDLE = 0,
    CONNECTING,
    CONNECTED,
    ABORTING,
    ERROR_CON,
    DUMMY
  };

  enum class OutcomeMessageCode : quint8 {
    TEXT_CMD     = 0x01,
    SETTINGS_MAP = 0x02,
    JOB          = 0x03,
    FORCE_OUT    = 0x04,
    PAIR         = 0x05
  };

private:
  enum class IncomeMessageCode : quint8 {
    STATE_VALUES  = 0x01,
    STATE_NAMES   = 0x02,
    SETTINGS_MAP  = 0x03,
    CONFIGURATION = 0x04,
    ALARM         = 0x05,
    JOB_FINISHED  = 0x06,
    USER_MSG      = 0x07
  };

public:
  explicit GateTcp(Logger           *logger,
                   QHostAddress      serverIp4Address,
                   quint16           port,
                   ConnectionDialog *connectionDialog,
                   QObject          *parent = nullptr);
  void connectToServer();

  void sendTextCmd(QString const &cmd);
  void sendSettingMap(QMap<QString, float> const &map);
  void sendGeneral(OutcomeMessageCode cmdCode, QByteArray data);
  void sendPair(QString const &text, float value);

signals:
  // Display RED BLINKING WARNING
  void showRedFlag(bool enable);

  void disconnected();

  // dsiable blinking warning
  void connected();

  //
  void settingsReceived(QMap<QString, float> plc_settings);

  //
  void stateListReceived(QStringList states);

  //
  void stateValuesListReceived(QVector<float> states);

  //
  void alarmReceived(quint32 alarmCode);

  //
  void jobFinishedReceived();

  //
  void userMsgReceived(QString userMsg);

private slots:
  void onConnected();
  void onDisconnected();
  void onError(QAbstractSocket::SocketError socketError);
  void onStateChanged(QAbstractSocket::SocketState socketState);
  void connectingTimeout();
  void setState(ConnectionState const state);

  // Writes any raw data to socket + log
  void sendData(QByteArray const &data);

  void sendKeepAlive();

  // Async handler to read data from socket.
  // As a first approximation, assume that the data packet is one and the message is complete.
  void onReadReady();

  // Parses incoming message.
  void parsePlcMessage(QByteArray const &data);

  bool parseStateNamesList(QByteArray data);

  bool parseStateValuesList(QByteArray data);

  bool parseSettingsMap(QByteArray data);

  bool parseAlarm(QByteArray data);

  bool parseUserMsg(QByteArray data);

private:
  Logger      *m_logger;
  QTcpSocket  *m_socket;
  StreamBuffer m_streambuffer;
  QHostAddress m_serverIp4Address;
  quint16      m_port;

  ConnectionState m_state;
  // QAbstractSocket::SocketState m_prevSocketState;
  quint16 m_contConnectingTryCount; // Непрерывное кол-во последовательных попыток подключения
  quint16 m_successConnectionCount; // Число удачных подключений (>1 -- имели место переподключения)
  bool              m_notifyTimeout;
  QTimer           *m_connectingTimer; // starts on tryConnect()
  QTimer           *m_keepAliveTimer;  // starts on onConnected()
  ConnectionDialog *m_connectionDialog;
  quint8            m_sendPacketCount;
};
} // namespace control2

#endif // GATETCP_H
