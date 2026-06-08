#ifndef STATUSMANAGER_H
#define STATUSMANAGER_H

#include <QObject>

namespace control2 {

class ResHolder;

/**
 * @brief Agregates:
 * tcp connection status ONLINE/OFFLINE/CONNECTION
 * chamber work/alarm/job status
 * user access level
 * opc-server status
 */
class StatusManager : public QObject {
  Q_OBJECT
public:
  // see init_flow.drawio
  enum class AppState {
    IDLE = 0,
    CONNECTING,
    CONNECTED,
    ONLINE,
    OFFLINE,
    DUMMY,
    WAIT_IN_DEBUG
  };

  explicit StatusManager(ResHolder *resHolder, QObject *parent = nullptr);

  // The only way to move in state machine.
  void                    setAppState(AppState const newState);
  StatusManager::AppState getAppState() const;
  void                    setOpcStatus(int newStatus);
  int                     getOpcStatus() const;

  // chamber verification
  void setStatesIdentical(bool value);
  void setSettingsListIdentical(bool value);
  bool getStatesIdentical() const;
  bool getSettingsListIdentical() const;

signals:
  void connectToServerRequested();
  void requestStateListRequested();
  void requestSettingListRequested();
  void requestOnline();
  void appStateChanged(StatusManager::AppState state);
  void opcStateChanged(int state);

private:
  ResHolder *m_resHolder;
  AppState   m_appState;
  int        m_opcState;

  // Lists of states and their order on the PLC and PC are identical.
  bool m_statesIdentical;
  // Lists of settings on the PLC and PC are identical (not values of params).
  bool m_settingsListIdentical;
};

inline StatusManager::AppState StatusManager::getAppState() const {
  return m_appState;
}

inline int StatusManager::getOpcStatus() const {
  return m_opcState;
}

inline void StatusManager::setStatesIdentical(bool value) {
  m_statesIdentical = value;
}

inline void StatusManager::setSettingsListIdentical(bool value) {
  m_settingsListIdentical = value;
}

inline bool StatusManager::getStatesIdentical() const {
  return m_statesIdentical;
}

inline bool StatusManager::getSettingsListIdentical() const {
  return m_settingsListIdentical;
}
} // namespace control2

#endif // STATUSMANAGER_H
