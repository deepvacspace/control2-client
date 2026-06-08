#include "app.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMessageBox>
#include <QThread>

#include "connectiondialog.h"
#include "constants.h"
#include "messagedialog.h"
#include "programmodel.h"
#include "settingsdiff.h"

namespace control2 {

App::App(bool windowMode, bool debugMode, QObject *parent)
    : QObject{parent}
    , m_status{&m_resHolder}
    , m_logger{"", 5, this}
    , m_users{nullptr}
    , m_gateTcp{nullptr}
    , m_mainWindow{nullptr}
    , m_alarmDialog{nullptr}
    , m_finishDialog{nullptr}
    , m_states{&m_logger, this}
    , m_settings{&m_logger, SETTINGS_FILE, this}
    , m_actions{&m_logger, ACTIONS_FILE, this}
    , m_hardStatus{&m_resHolder}
    , m_opcThread{nullptr}
    , m_opcServer{nullptr}
    , m_windowMode{windowMode}
    , m_debugMode{debugMode} {
  m_resHolder.states      = &m_states;
  m_resHolder.settings    = &m_settings;
  m_resHolder.logger      = &m_logger;
  m_resHolder.status      = &m_status;
  m_resHolder.actions     = &m_actions;
  m_resHolder.hard_status = &m_hardStatus;
  m_resHolder.usb         = &m_usbManager;

  m_logger.setLogLevel(Logger::Level::DEB);

  // timers
  m_paramsRequestTimer = new QTimer(this);
  m_paramsRequestTimer->setSingleShot(true);
  connect(m_paramsRequestTimer, &QTimer::timeout, this, &App::requestStateList);

  m_settingsRequestTimer = new QTimer(this);
  m_settingsRequestTimer->setSingleShot(true);
  connect(m_settingsRequestTimer, &QTimer::timeout, this, &App::requestSettingList);

  connect(&m_status, &StatusManager::connectToServerRequested, this, &App::connectToServer);
  connect(&m_status, &StatusManager::requestStateListRequested, this, &App::requestStateList);
  connect(&m_status, &StatusManager::requestSettingListRequested, this, &App::requestSettingList);
  connect(&m_status, &StatusManager::requestOnline, this, &App::startOnline);

  initSettings();

  QTimer::singleShot(1000, [this]() {
    QVector<float> stateValues(m_states.size(), 1.23e-8);
    m_states.setFloatValues(stateValues);
  });
}

App::~App() {
  if (m_mainWindow) {
    delete m_mainWindow;
  }
  stopOpc();
  m_actions.addAction(ActionManager::ActionType::APP, tr("Закрытие приложения"));
  m_logger.log(Logger::Level::INFO, "bye-bye");
}

void App::start() {
  m_status.setAppState(StatusManager::AppState::IDLE);
}

UiLanguage App::uiLanguage() const noexcept {
  return m_settings.uiLanguage();
}

void App::initSettings() {
  bool const result{m_settings.loadFromSettingsFile()};
  if (!result) {
    QMessageBox::critical(
      nullptr, tr("Критическая ошибка"), QString(tr("Не удалось прочитать файл настроек '%1'")).arg(SETTINGS_FILE));
    // todo: maybe there is better solution
    m_logger.stream(Logger::Level::ERR) << tr("Не удалось прочитать файл настроек") << SETTINGS_FILE;
    exit(1);
  }
}

void App::runtimeSetups() {
  auto const settingMapVariant = m_settings.settingsMap();

  // GATE
  // todo: Add settings checks
  QVariantMap const tcpServerMap{settingMapVariant["tcp_server"].toMap()};
  QString const     serveIp4Address{tcpServerMap["server_ip"].toString()};
  quint16 const     serverPort{static_cast<quint16>(tcpServerMap["server_port"].toUInt())};
  QString const     serverId{QString("%1:%2").arg(serveIp4Address).arg(serverPort)};
  m_gateTcp = new GateTcp(&m_logger, QHostAddress(serveIp4Address), serverPort, new ConnectionDialog(serverId), this);
  m_resHolder.tcpGate = m_gateTcp;

  connect(m_gateTcp, &GateTcp::connected, this, &App::onConnectToServer);
  connect(m_gateTcp, &GateTcp::disconnected, this, &App::onDisconnectFromServer);
  connect(m_gateTcp, &GateTcp::stateListReceived, this, &App::onStateNamesReceive);
  connect(m_gateTcp, &GateTcp::settingsReceived, this, &App::onSettingsMapReceived);
  connect(m_gateTcp, &GateTcp::stateValuesListReceived, this, &App::onStateValuesReceive);
  connect(m_gateTcp, &GateTcp::alarmReceived, this, &App::onAlarmReceived);
  connect(m_gateTcp, &GateTcp::jobFinishedReceived, this, &App::onJobFinishedReceived);

  // Load states
  m_states.addWithVariantList(settingMapVariant["states"].toList());

  // Data manager
  m_dataManager           = new DataManager(&m_resHolder, m_states.size(), control2::INTERVAL_DURATION, this);
  m_resHolder.dataManager = m_dataManager;

  // Make default program, todo: add settings checks
  m_resHolder.defaultProgram
    = new ProgramModel{settingMapVariant["control_params"].toList(), ProgramModel::ProgType::PROG, this};
  m_resHolder.defaultProgram->setIntervalFromVariantMap(0, settingMapVariant["default_program"].toMap());

  //
  m_users           = new AccessManager(&m_resHolder, &m_logger, this);
  m_resHolder.users = m_users;

  m_actions.addAction(ActionManager::ActionType::APP,
                      QString(tr("Запуск приложения. Пользователь '%1'")).arg(m_users->getCurrentUserName()));

  // Init UI window
  m_mainWindow = new MainWindow(&m_resHolder, m_windowMode);
  connect(m_mainWindow, &MainWindow::aboutClosed, this, &App::stopOpc);
  m_mainWindow->show();
  if (!m_windowMode) {
    m_mainWindow->showFullScreen();
  }
  m_hardStatus.requestStatusEmit(); // to update window

  // Create Alarm dialog
  m_alarmDialog = new AlarmDialog(m_mainWindow);

  connect(m_alarmDialog, &AlarmDialog::accepted, this, [this]() {
    m_gateTcp->sendTextCmd("confrim_alarm");
  });

  // Second way to display finish confrim dialog
  connect(
    &m_hardStatus,
    &HardwareStatus::statusChanged,
    this,
    [this](HardwareStatus::HardwareStatusEnum const &newStatus, HardwareStatus::HardwareStatusEnum const &oldStatus) {
      if (HardwareStatus::HardwareStatusEnum::FINISH == newStatus) {
        onJobFinishedReceived();
      }
    });

  connect(m_gateTcp, &GateTcp::jobFinishedReceived, this, [this]() {
    m_actions.addAction(ActionManager::ActionType::PLC, tr("Работа камеры завешена"));
  });

  connect(m_gateTcp, &GateTcp::showRedFlag, m_mainWindow, &MainWindow::setConnectionErrorFlash);
}

void App::connectToServer() {
  m_paramsRequestTimer->stop();
  m_settingsRequestTimer->stop();

  m_gateTcp->connectToServer(); // igonored if socket in connecting state already
}

void App::onConnectToServer() {
  m_status.setAppState(StatusManager::AppState::CONNECTED);
}

void App::onDisconnectFromServer() {
  m_status.setAppState(StatusManager::AppState::CONNECTING);
}

void App::onStateNamesReceive(QStringList states) {
  m_paramsRequestTimer->stop();
  if (states == m_states.getNames()) {
    m_status.setStatesIdentical(true);
    m_logger.stream(Logger::Level::INFO) << tr("Список переменных состояний на ПЛК и ПК совпадают");
  } else {
    QMessageBox::critical(
      nullptr, tr("Критическая ошибка"), QString(tr("Переменные состояний на ПЛК и ПК отличаются")));
    m_logger.stream(Logger::Level::ERR) << tr("Список переменных состояний на ПЛК и ПК отличаются");
    m_logger.stream(Logger::Level::ERR) << "PLC:" << states;
    m_logger.stream(Logger::Level::ERR) << "PC:" << m_states.getNames();
    // todo: maybe there is better solution
    exit(1);
  }

  checkConfigIdentical();
}

void App::onSettingsMapReceived(QMap<QString, float> plcSettings) {
  m_settingsRequestTimer->stop();
  m_logger.stream(Logger::Level::INFO) << plcSettings.keys();

  auto const localSetting = m_settings.getSettingPairs();

  if (plcSettings == localSetting) {
    if (m_status.getSettingsListIdentical()) { // not first time
      auto *msgDlg = new MessageDialog(tr("Подтверждение"), tr("Настройки совпадают"));
      msgDlg->open();
    }
    m_status.setSettingsListIdentical(true);
    m_logger.stream(Logger::Level::INFO) << tr("Список настроек на ПЛК и ПК совпадают");
  } else {
    QMessageBox::critical(nullptr, tr("Внимание"), QString(tr("Настройки на ПЛК и ПК отличаются")));
    m_logger.stream(Logger::Level::WAR) << tr("Настройки на ПЛК и ПК отличаются");

    auto *diffDialog = new SettingsDiff(localSetting, plcSettings);
    connect(diffDialog, &QDialog::finished, this, [this, localSetting, plcSettings](int result) {
      switch (result) {
        case 2: // sendBtn
          // setState(State::OFFLINE);
          m_gateTcp->sendSettingMap(localSetting);
          break;
        case 3: // getBtn
          m_settings.applySettingsFromPairs(plcSettings);
          m_status.setSettingsListIdentical(true); // assume settings are equeal now
          break;
        default: // QDialog::Rejected:
          m_status.setAppState(StatusManager::AppState::OFFLINE);
          break;
      }
    });
    diffDialog->open();

    // todo: maybe there is better solution
    // exit(1);
  }

  checkConfigIdentical();
}

void App::onAlarmReceived(quint32 alarmCode) {
  m_logger.stream(Logger::Level::INFO) << utils::binaryString(
    static_cast<char const *>(static_cast<void const *>(&alarmCode)), 4, true);

  QStringList alarmLabelsList;
  quint32     i{0U};
  while ((alarmCode > 0) && (i < 32U)) {
    bool const ithAlarm{static_cast<bool>(alarmCode & 0x01)};
    if (ithAlarm) {
      QString const alarmLabel{m_settings.getAlarmsMap().contains(i) ? m_settings.getAlarmsMap()[i]
                                                                     : tr("Неизвестная ошибка")};
      QString const alarmText{QString("%1. %2").arg(i + 1).arg(alarmLabel)};
      alarmLabelsList.push_back(alarmText);
    }
    alarmCode >>= 1;
    ++i;
  }

  m_alarmDialog->showAlarmsList(alarmLabelsList);

  m_actions.addAction(ActionManager::ActionType::ERR, tr("Ошибка") + ": " + alarmLabelsList.join(" "));
}

void App::onJobFinishedReceived() {
  if (nullptr == m_finishDialog) {
    m_finishDialog = new MessageDialog(tr("Окончание работы"), tr("Камера завершила работу"), m_mainWindow);
    connect(m_finishDialog, &MessageDialog::finished, this, [this](int) {
      m_gateTcp->sendTextCmd("confrim_finish");
      m_finishDialog->deleteLater();
      m_finishDialog = nullptr;
    });
    m_finishDialog->open();
  }
}

void App::onStateValuesReceive(QVector<float> stateValues) {
  if (StatusManager::AppState::ONLINE != m_status.getAppState()) {
    return;
  }

  if (stateValues.size() != m_states.size()) {
    QString const errMsg{QString(tr("Количество значений (%1) отличается от количества переменных состояний (%2)"))
                           .arg(stateValues.size())
                           .arg(m_states.size())};
    QMessageBox::critical(nullptr, tr("Критическая ошибка"), errMsg);
    m_logger.stream(Logger::Level::ERR) << errMsg;
    return;
  }

  m_states.setFloatValues(stateValues);

  QVector<double> valuesDouble{stateValues.cbegin(), stateValues.cend()};
  m_dataManager->addPoint(static_cast<double>(QDateTime::currentSecsSinceEpoch()), valuesDouble);

  if (m_opcServer && (0 == m_status.getOpcStatus())) {
    QVector<double> opcStateValuesVector;
    for (auto const &opcStateName : m_opcStates) {
      opcStateValuesVector.push_back(static_cast<double>(m_states.getSysValue(opcStateName)->getValue()));
    }
    m_opcServer->updateValues(m_states.getSysValue("status_condition")->getValue(), opcStateValuesVector);
  }
}

void App::checkConfigIdentical() {
  if (m_status.getStatesIdentical() && m_status.getSettingsListIdentical()) {
    m_status.setAppState(StatusManager::AppState::ONLINE);
  }
}

void App::setupOpc() {
  QVariantMap const appSettings{m_settings.settingsMap()["app_settings"].toMap()};
  if (appSettings["opc_server"].toBool() && m_settings.settingsMap().contains("opc_settings")) {
    QVariantMap const opcSettings{m_settings.settingsMap()["opc_settings"].toMap()};
    if (opcSettings["port"].isValid() && opcSettings["period_ms"].isValid() && opcSettings["root_name"].isValid()) {
      QVariantMap opcSetupSettings;
      opcSetupSettings["server_root_name"] = opcSettings["root_name"].toString();
      opcSetupSettings["port"]             = opcSettings["port"].toUInt();
      opcSetupSettings["period_msec"]      = opcSettings["period_ms"].toUInt();

      QStringList tags{};
      for (QVariant tagVariant : opcSettings["tags"].toList()) {
        QVariantMap const tagVariantMap{tagVariant.toMap()};

        QString const tagString   = tagVariantMap["tag"].toString();
        QString const stateString = tagVariantMap["state"].toString();

        tags.push_back(tagString);
        m_opcStates.push_back(stateString);
      }

      m_opcServer = new TestaOpcServer(opcSetupSettings, tags);
      m_opcThread = new QThread(this);
      m_opcServer->moveToThread(m_opcThread);
      connect(m_opcThread, &QThread::started, m_opcServer, &TestaOpcServer::init);
      connect(m_opcThread, &QThread::finished, m_opcServer, &TestaOpcServer::stop);
      connect(m_opcThread, &QThread::finished, m_opcServer, &QThread::deleteLater);
      connect(m_opcThread, &QThread::finished, m_opcThread, &QThread::deleteLater);

      connect(m_opcServer, &TestaOpcServer::initialized, &m_status, &StatusManager::setOpcStatus);

      m_opcThread->start();
    }
  }
  if (!m_opcThread) {
    m_status.setOpcStatus(-2); // OPC OFF
  }
}

// После установления соединения с оборудованием
void App::startOnline() {
  // start OPC if need
  setupOpc();

  if (m_states.contains("status_condition")) {
    m_hardStatus.setup(true, m_states.getSysValue("status_condition"));
  }
}

void App::stopOpc() {
  if (m_opcThread) {
    m_opcThread->quit();
    m_opcThread->wait();
  }
}

void App::requestStateList() {
  if (m_status.getStatesIdentical()) {
    return;
  }
  auto period = m_settings.settingsMap()["communication"].toMap()["base_params_request_period_sec"].toInt();
  m_paramsRequestTimer->start(1000 * period);

  m_gateTcp->sendTextCmd("get_states");
}

void App::requestSettingList() {
  if (m_status.getSettingsListIdentical()) {
    return;
  }
  auto period = m_settings.settingsMap()["communication"].toMap()["base_params_request_period_sec"].toInt();
  m_settingsRequestTimer->start(1000 * period);

  m_gateTcp->sendTextCmd("get_settings");
}

} // namespace control2
