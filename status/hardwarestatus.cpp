#include "hardwarestatus.h"

#include "resholder.h"

namespace deepvaccontrol {
HardwareStatus::HardwareStatus(ResHolder *resHolder, QObject *parent)
    : QObject{parent}
    , m_resHolder{resHolder}
    , m_enable{false}
    , m_statusSysVal{nullptr}
    , m_status{HardwareStatusEnum::NONE} {
}

void HardwareStatus::setup(bool enable, SystemValue *statusSysVal) {
  connect(m_resHolder->status, &StatusManager::appStateChanged, this, [this](StatusManager::AppState state) {
    if (state != StatusManager::AppState::CONNECTED) {
      changeStatus(HardwareStatusEnum::NONE);
    }
  });

  m_enable = enable;
  if (!enable) {
    changeStatus(HardwareStatusEnum::NONE);

    if (true == static_cast<bool>(m_updateSysValConnection)) {
      if (disconnect(m_updateSysValConnection)) {
        m_resHolder->logger->stream(Logger::Level::DEB)
          << "valueChanged signal has been successfully disconeted from prev statusSysVal";
      } else {
        m_resHolder->logger->stream(Logger::Level::DEB)
          << "valueChanged signal has been UNSUCCESSFULLY disconeted from prev statusSysVal";
      }
    }
    return;
  }

  m_statusSysVal = statusSysVal;
  m_updateSysValConnection
    = connect(m_statusSysVal, &SystemValue::valueChanged, this, &HardwareStatus::onSystemValueChanged);
  if (true == static_cast<bool>(m_updateSysValConnection)) {
    m_resHolder->logger->stream(Logger::Level::DEB)
      << "valueChanged signal has been successfully connected for new statusSysVal";
  } else {
    m_resHolder->logger->stream(Logger::Level::DEB)
      << "valueChanged signal has been UNSUCCESSFULLY connected for new statusSysVal";
  }
  onSystemValueChanged(m_statusSysVal->getValue());
}

void HardwareStatus::requestStatusEmit() {
  emit statusChanged(m_status, m_status);
}

void HardwareStatus::changeStatus(HardwareStatusEnum const &newStatus) {
  HardwareStatusEnum const oldStatus{m_status};
  m_status = newStatus;
  emit statusChanged(newStatus, oldStatus);
}

void HardwareStatus::onSystemValueChanged(float value) {
  HardwareStatusEnum const statusFromValue{static_cast<HardwareStatusEnum>(static_cast<quint32>(value))};

  if (statusFromValue != m_status) {
    changeStatus(statusFromValue);
  }
}

// void Status::setWorkStatus(WorkStatus const newStatus, bool const toLog) {
//   WorkStatus const oldStatus{m_workStatus};
//   //        STATUS_CONDITION_IDLE:=1,
//   //        STATUS_CONDITION_WORK,
//   //        STATUS_CONDITION_DEL,
//   //        STATUS_CONDITION_FINISH,
//   //        STATUS_CONDITION_ERROR
//   //        STATUS_CONDITION_PAUSE
//   //        STATUS_CONDITION_DIAGNOSTIC

//   if (newStatus == oldStatus) {
//     if (toLog) {
//       m_logStatus = newStatus;
//     }
//     return;
//   }

//   if (toLog && (ALARM == newStatus) && (ALARM != m_logStatus)) {
//     emit newAlarmCondition();
//   }

//   m_workStatus = newStatus;
//   if (toLog) {
//     m_logStatus = newStatus;
//   }

//   emit statusUpdated(m_workStatus, oldStatus);
// }

// Status::WorkStatus Status::getWorkStatus() const {
//   return m_workStatus;
// }
} // namespace deepvaccontrol
