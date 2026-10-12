#include "app.h"

#include "experimentrecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QMessageBox>
#include <QThread>

#include "confdialog.h"
#include "connectiondialog.h"
#include "constants.h"
#include "messagedialog.h"
#include "programmodel.h"
#include "settingsdiff.h"
#include "settingsrecoverydialog.h"
#include "statemismatchdialog.h"

namespace deepvaccontrol {

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
    , m_dataManager{nullptr}
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
  m_actions.addAction(ActionManager::ActionType::APP, tr("Application closed")); //Закрытие приложения
  m_logger.log(Logger::Level::INFO, "Goodbye");
}

void App::start() {
  m_status.setAppState(StatusManager::AppState::IDLE);
  if (!m_settingsNotice.isEmpty()) {
    // settings.json was replaced by a backup at start (initSettings)
    m_actions.addAction(ActionManager::ActionType::ERR, m_settingsNotice);
    QTimer::singleShot(0, this, [this] { (new MessageDialog(tr("Warning"), m_settingsNotice, m_mainWindow))->open(); });
  }
}

UiLanguage App::uiLanguage() const noexcept {
  return m_settings.uiLanguage();
}

void App::initSettings() {
  if (m_settings.loadFromSettingsFile()) {
    keepSettingsBackup();
    return;
  }
  QString problem = SettingsManager::fileProblem(SETTINGS_FILE);
  m_logger.stream(Logger::Level::ERR) << SETTINGS_FILE << "cannot be used:" << problem;

  // the last copy that loaded correctly replaces it without asking
  if (SettingsManager::fileProblem(SETTINGS_BACKUP_FILE).isEmpty() && restoreSettings(SETTINGS_BACKUP_FILE)) {
    m_settingsNotice = tr("settings.json could not be used: %1 It was replaced by settings_backup.json, the last copy that "
                          "loaded correctly (saved %2). Changes made after that copy are lost.")
                         .arg(problem, QFileInfo(SETTINGS_BACKUP_FILE).lastModified().toString("yyyy-MM-dd HH:mm"));
    return;
  }

  // nothing to fall back on: explain and let the operator choose
  SettingsRecoveryDialog dialog(SETTINGS_FILE, problem);
  while (true) {
    int const choice = dialog.exec();
    if (SettingsRecoveryDialog::RETRY == choice) {
      if (m_settings.loadFromSettingsFile()) {
        keepSettingsBackup();
        return;
      }
      problem = SettingsManager::fileProblem(SETTINGS_FILE);
    } else if (SettingsRecoveryDialog::RESTORE == choice) {
      QString const backup = dialog.selectedBackup();
      if (restoreSettings(backup)) {
        m_settingsNotice = tr("settings.json could not be used: %1 It was replaced by %2.").arg(problem, QFileInfo(backup).fileName());
        return;
      }
      problem = tr("%1 could not be restored either: %2").arg(QFileInfo(backup).fileName(), SettingsManager::fileProblem(backup));
    } else {
      m_logger.stream(Logger::Level::ERR) << "Closed at start: no usable settings file";
      exit(1);
    }
    m_logger.stream(Logger::Level::ERR) << SETTINGS_FILE << "cannot be used:" << problem;
    dialog.setProblem(problem);
  }
}

void App::keepSettingsBackup() {
  // copy of a settings.json that loads correctly, written safely (never a half copy)
  QFile source(SETTINGS_FILE);
  QSaveFile copy(SETTINGS_BACKUP_FILE);
  if (source.open(QIODevice::ReadOnly) && copy.open(QIODevice::WriteOnly)) {
    copy.write(source.readAll());
    if (!copy.commit()) {
      m_logger.stream(Logger::Level::WAR) << "Could not update" << SETTINGS_BACKUP_FILE;
    }
  }
}

bool App::restoreSettings(QString const &backup) {
  if (!m_settings.loadFrom(backup)) {
    return false;
  }
  // the unusable file is kept aside for a look, then settings.json is written from the backup
  if (QFile::exists(SETTINGS_FILE)) {
    QString const aside = QStringLiteral("settings_unusable_%1.json").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    QFile::rename(SETTINGS_FILE, aside);
    m_logger.stream(Logger::Level::WAR) << "Unusable" << SETTINGS_FILE << "kept as" << aside;
  }
  m_settings.saveToSettingsFile();
  m_logger.stream(Logger::Level::WAR) << SETTINGS_FILE << "restored from" << backup;
  keepSettingsBackup(); // a broken settings_backup.json is replaced as well
  return true;
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
  // another chamber (Settings -> TCP): its value list and settings are checked again before it is used
  connect(m_gateTcp, &GateTcp::serverChanged, this, [this] {
    m_status.setStatesIdentical(false);
    m_status.setSettingsListIdentical(false);
    m_mismatchAddress.clear();
  });
  connect(m_gateTcp, &GateTcp::stateListReceived, this, &App::onStateNamesReceive);
  connect(m_gateTcp, &GateTcp::settingsReceived, this, &App::onSettingsMapReceived);
  connect(m_gateTcp, &GateTcp::stateValuesListReceived, this, &App::onStateValuesReceive);
  connect(m_gateTcp, &GateTcp::alarmReceived, this, &App::onAlarmReceived);
  connect(m_gateTcp, &GateTcp::jobFinishedReceived, this, &App::onJobFinishedReceived);

  // Load states
  m_states.addWithVariantList(settingMapVariant["states"].toList());

  // Data manager
  m_dataManager           = new DataManager(&m_resHolder, m_states.size(), deepvaccontrol::INTERVAL_DURATION, this);
  m_resHolder.dataManager = m_dataManager;

  connect(&m_settings, &SettingsManager::opcSettingsChanged, this, &App::restartOpc);

  // Make default program, todo: add settings checks
  m_resHolder.defaultProgram
    = new ProgramModel{settingMapVariant["control_params"].toList(), ProgramModel::ProgType::PROG, this};
  m_resHolder.defaultProgram->setIntervalFromVariantMap(0, settingMapVariant["default_program"].toMap());

  //
  m_users           = new AccessManager(&m_resHolder, &m_logger, this);
  m_resHolder.users = m_users;

  m_actions.addAction(ActionManager::ActionType::APP,
                      QString(tr("Application started. User '%1'")).arg(m_users->getCurrentUserName()));

  // Init UI window
  m_mainWindow = new MainWindow(&m_resHolder, m_windowMode);
  connect(m_mainWindow, &MainWindow::aboutClosed, this, &App::stopOpc);
  m_mainWindow->showMaximized();
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
    m_actions.addAction(ActionManager::ActionType::PLC, tr("Chamber operation completed"));
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
  if (states != m_states.getNames()) {
    handleStateMismatch(states);
    return;
  }
  m_status.setStatesIdentical(true);
  m_lastGoodAddress = m_gateTcp->serverAddress();
  m_mismatchAddress.clear();
  m_logger.stream(Logger::Level::INFO) << tr("PLC and PC state variable lists match");
  requestSettingList(); // compared once the values are known to match

  checkConfigIdentical();
}

void App::handleStateMismatch(QStringList const &chamberStates) {
  QString const address = m_gateTcp->serverAddress();
  m_status.setStatesIdentical(false);
  m_settingsRequestTimer->stop();
  m_status.setAppState(StatusManager::AppState::OFFLINE); // values ignored, nothing sent
  m_logger.stream(Logger::Level::ERR) << "Chamber" << address << "reports another value list than settings.json";
  m_logger.stream(Logger::Level::ERR) << "PLC:" << chamberStates;
  m_logger.stream(Logger::Level::ERR) << "PC:" << m_states.getNames();
  if (m_mismatchAddress == address) {
    return; // explained already (a reconnection of the same chamber)
  }
  m_mismatchAddress = address;
  m_actions.addAction(ActionManager::ActionType::ERR,
                      tr("Chamber %1 not used: its values differ from this application's configuration").arg(address));

  // values this application cannot work without: adopting a list that lacks them is not offered
  QVariantMap const settings = m_settings.settingsMap();
  QStringList       needed{"temp", "temp_ref", "status_condition"};
  for (QVariant const &entry : settings.value("monitor").toList()) {
    needed.push_back(entry.toMap().value("name").toString());
  }
  for (QVariant const &entry : settings.value("plc_settings").toMap().value("calibrations").toList()) {
    QString const state = entry.toMap().value("state_name").toString();
    needed << state << state + "_raw";
  }
  QStringList missing;
  for (QString const &name : std::as_const(needed)) {
    if (!name.isEmpty() && m_states.contains(name) && !chamberStates.contains(name) && !missing.contains(name)) {
      missing.push_back(name);
    }
  }

  auto *dialog = new StateMismatchDialog(address, chamberStates, m_states.getNames(),
                                         m_lastGoodAddress == address ? QString() : m_lastGoodAddress, missing, m_mainWindow);
  connect(dialog, &QDialog::finished, this, [this, chamberStates](int choice) {
    if (StateMismatchDialog::BACK == choice) {
      switchBack();
    } else if (StateMismatchDialog::ADOPT == choice) {
      adoptChamberStates(chamberStates);
    }
  });
  dialog->open();
}

void App::switchBack() {
  QString const   address = m_lastGoodAddress;
  qsizetype const colon   = address.lastIndexOf(':');
  if (colon <= 0) {
    return;
  }
  QString const host = address.left(colon);
  auto const    port = static_cast<quint16>(address.mid(colon + 1).toUInt());
  m_settings.setTcpServer(host, port);
  m_gateTcp->setServer(QHostAddress(host), port);
  m_mainWindow->setWindowTitle(QStringLiteral("Deepvac Control (%1)").arg(host));
}

void App::adoptChamberStates(QStringList const &chamberStates) {
  QString const backup = QStringLiteral("settings_backup_%1.json").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
  auto *confirm = new ConfDialog(
    tr("Use the chamber's value list?"),
    tr("settings.json gets the chamber's %1 values: the known ones keep their names, units and colours, new ones are "
       "added hidden on the chart, and OPC variables of values that are gone are dropped. The current file is kept as "
       "%2. The application then restarts.")
      .arg(chamberStates.size())
      .arg(backup),
    tr("Use and restart"), tr("Cancel"), m_mainWindow);
  connect(confirm, &QDialog::accepted, this, [this, chamberStates, backup] {
    if (!QFile::copy(SETTINGS_FILE, backup)) {
      (new MessageDialog(tr("Error"), tr("No backup of settings.json could be written, nothing was changed."), m_mainWindow))->open();
      return;
    }
    QVariantMap const           settings = m_settings.settingsMap();
    QHash<QString, QVariantMap> known;
    for (QVariant const &entry : settings.value("states").toList()) {
      known.insert(entry.toMap().value("name").toString(), entry.toMap());
    }
    QVariantList states;
    for (QString const &name : chamberStates) {
      states.push_back(known.contains(name) ? known.value(name)
                                            : QVariantMap{{"name", name}, {"label", name}, {"visible", false},
                                                          {"color", "#64748b"}, {"width", 2}, {"pen", 1}});
    }
    QVariantMap  opc = settings.value("opc_settings").toMap();
    QVariantList tags;
    for (QVariant const &tag : opc.value("tags").toList()) {
      if (chamberStates.contains(tag.toMap().value("state").toString())) {
        tags.push_back(tag);
      }
    }
    opc["tags"] = tags;
    if (!m_settings.setEntries({{"states", states}, {"opc_settings", opc}})) {
      (new MessageDialog(tr("Error"), tr("settings.json could not be written; the backup %1 is unchanged.").arg(backup), m_mainWindow))
        ->open();
      return;
    }
    m_logger.stream(Logger::Level::INFO) << "settings.json takes the value list of" << m_gateTcp->serverAddress()
                                         << "(backup" << backup << ") - restarting";
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QCoreApplication::arguments().mid(1), QDir::currentPath());
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
  });
  confirm->open();
}

void App::onSettingsMapReceived(QMap<QString, float> plcSettings) {
  m_settingsRequestTimer->stop();
  m_logger.stream(Logger::Level::INFO) << plcSettings.keys();

  auto const localSetting = m_settings.getSettingPairs();

  if (plcSettings == localSetting) {
    if (m_status.getSettingsListIdentical()) { // not first time
      auto *msgDlg = new MessageDialog(tr("Confirmation"), tr("Settings match"));
      msgDlg->open();
    }
    m_status.setSettingsListIdentical(true);
    m_logger.stream(Logger::Level::INFO) << tr("PLC and PC settings lists match");
  } else {
    QMessageBox::critical(nullptr, tr("Warning"), QString(tr("PLC and PC settings differ")));
    m_logger.stream(Logger::Level::WAR) << tr("PLC and PC settings differ");

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
                                                                     : tr("Unknown error")};
      QString const alarmText{QString("%1. %2").arg(i + 1).arg(alarmLabel)};
      alarmLabelsList.push_back(alarmText);
    }
    alarmCode >>= 1;
    ++i;
  }

  m_alarmDialog->showAlarmsList(alarmLabelsList);

  m_actions.addAction(ActionManager::ActionType::ERR, tr("Error") + ": " + alarmLabelsList.join(" "));
}

void App::onJobFinishedReceived() {
  if (nullptr == m_finishDialog) {
    m_finishDialog = new MessageDialog(tr("Operation complete"), tr("Chamber operation completed"), m_mainWindow);
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
    QString const errMsg{QString(tr("Number of values (%1) differs from number of state variables (%2)"))
                           .arg(stateValues.size())
                           .arg(m_states.size())};
    QMessageBox::critical(nullptr, tr("Critical error"), errMsg);
    m_logger.stream(Logger::Level::ERR) << errMsg;
    return;
  }

  m_states.setFloatValues(stateValues);

  QVector<double> valuesDouble{stateValues.cbegin(), stateValues.cend()};
  qint64 const    timestampMs = QDateTime::currentMSecsSinceEpoch();
  // millisecond resolution: samples arriving within the same second are all kept
  m_dataManager->addPoint(static_cast<double>(timestampMs) / 1000.0, valuesDouble);
  if (m_resHolder.recorder) {
    m_resHolder.recorder->addSample(static_cast<double>(timestampMs) / 1000.0, valuesDouble); // experiment database
  }

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
  // start OPC if need (after a reconnection or a chamber switch the running server is kept)
  if (!m_opcThread) {
    setupOpc();
  }

  if (m_states.contains("status_condition")) {
    m_hardStatus.setup(true, m_states.getSysValue("status_condition"));
  }
}

void App::stopOpc() {
  if (m_opcThread) {
    m_opcThread->quit();
    m_opcThread->wait();
    // both are deleted by the thread's finished() connections
    m_opcThread = nullptr;
    m_opcServer = nullptr;
  }
  m_opcStates.clear();
}

void App::restartOpc() {
  stopOpc();
  m_status.setOpcStatus(-2); // OPC OFF
  if (StatusManager::AppState::ONLINE == m_status.getAppState()) {
    setupOpc();
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
  if (m_status.getSettingsListIdentical() || !m_status.getStatesIdentical()) {
    return; // asked again by onStateNamesReceive() once the value list matches
  }
  auto period = m_settings.settingsMap()["communication"].toMap()["base_params_request_period_sec"].toInt();
  m_settingsRequestTimer->start(1000 * period);

  m_gateTcp->sendTextCmd("get_settings");
}

} // namespace deepvaccontrol
