#include "statusmanager.h"

#include "constants.h"
#include "resholder.h"

namespace deepvaccontrol {
StatusManager::StatusManager(ResHolder *resHolder, QObject *parent)
    : QObject{parent}
    , m_opcState{-2}
    , m_resHolder{resHolder}
    , m_appState{AppState::DUMMY}
    , m_statesIdentical{false}
    , m_settingsListIdentical{false} {
}

void StatusManager::setAppState(AppState const newState) {
  if (newState == m_appState) {
    return;
  }

  m_appState = newState;
  emit appStateChanged(newState);
  m_resHolder->logger->stream(Logger::Level::DEB) << deepvaccontrol::APP_STATE_LIST[static_cast<quint32>(newState)];

  switch (newState) {
    case AppState::IDLE: // starting point
      setAppState(AppState::CONNECTING);
      break;

    case AppState::CONNECTING:
      emit connectToServerRequested();
      break;

    case AppState::CONNECTED:
      if (m_statesIdentical && m_settingsListIdentical) {
        setAppState(AppState::ONLINE);
      } else {
        emit requestStateListRequested();
        emit requestSettingListRequested();
      }
      break;

    case AppState::ONLINE:
      emit requestOnline();
      break;

    case AppState::OFFLINE:
      break;

    case AppState::DUMMY:
      break;

    case AppState::WAIT_IN_DEBUG:
      break;

    default:
      break;
  }
}

void StatusManager::setOpcStatus(int newState) {
  m_opcState = newState;
  m_resHolder->logger->stream(Logger::Level::INFO) << "OPC Ua Server initialized with status:" << newState;
  emit opcStateChanged(m_opcState);
}
} // namespace deepvaccontrol
