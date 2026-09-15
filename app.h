#ifndef APP_H
#define APP_H

#include <QObject>
#include <QThread>
#include <QTimer>

#include "accessmanager.h"
#include "actionmanager.h"
#include "alarmdialog.h"
#include "datamanager.h"
#include "gatetcp.h"
#include "hardwarestatus.h"
#include "logger.h"
#include "mainwindow.h"
#include "messagedialog.h"
#include "resholder.h"
#include "settingsmanager.h"
#include "statusmanager.h"
#include "systemvaluelist.h"
#include "testa_opc_server.h"
#include "usbmanager.h"

namespace deepvaccontrol {
class App : public QObject {
  Q_OBJECT
public:
  explicit App(bool windowMode = false, bool debugMode = false, QObject *parent = nullptr);
  ~App() override;
  void       start();
  UiLanguage uiLanguage() const noexcept;
  void runtimeSetups();      // right after loading settings file

private:
  void initSettings();
  void requestStateList();   // driven by m_paramsRequestTimer
  void requestSettingList(); // driven by m_settingsRequestTimer
  void connectToServer();
  void onConnectToServer();
  void onDisconnectFromServer();
  void onStateNamesReceive(QStringList states);
  void onStateValuesReceive(QVector<float> stateValues);
  void onSettingsMapReceived(QMap<QString, float> plcSettings);
  void onAlarmReceived(quint32 alarmCode);
  void onJobFinishedReceived();
  void checkConfigIdentical();
  // 0 - ok, -1 - error, -2 - disabled
  void setupOpc();
  void stopOpc();
  void startOnline();

  ResHolder m_resHolder;

  StatusManager m_status;

  Logger m_logger;

  AccessManager *m_users;

  // Tcp connection handler.
  GateTcp *m_gateTcp;

  // Main application window.
  MainWindow    *m_mainWindow;
  AlarmDialog   *m_alarmDialog;
  MessageDialog *m_finishDialog;

  SystemValueList m_states;

  SettingsManager m_settings;

  DataManager *m_dataManager;

  ActionManager m_actions;

  HardwareStatus m_hardStatus;

  QThread        *m_opcThread;
  TestaOpcServer *m_opcServer;
  QStringList     m_opcStates; // list of tags/states to updated in opc server

  UsbManager m_usbManager;

  QTimer *m_paramsRequestTimer;
  QTimer *m_settingsRequestTimer;

  bool m_windowMode;
  bool m_debugMode;
};
} // namespace deepvaccontrol

#endif // APP_H
