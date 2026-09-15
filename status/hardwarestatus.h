#ifndef HARDWARESTATUS_H
#define HARDWARESTATUS_H

#include <QObject>

#include "logger.h"
#include "systemvalue.h"

namespace deepvaccontrol {

class ResHolder;

class HardwareStatus : public QObject {
  Q_OBJECT
public:
  enum HardwareStatusEnum {
    NONE = 0,
    IDLE,
    WORK,
    FINISH,
    ALARM
  };
  enum JobType {
    NONE_JOB = 0,
    SETPOINT = 1,
    PROGRAM  = 2
  };
  explicit HardwareStatus(ResHolder *resHolder, QObject *parent = nullptr);

  // option 1: enable=true + statusSystVal != 0
  // option 2: enable=false
  void setup(bool enable, SystemValue *statusSystVal = nullptr);

  HardwareStatusEnum getHardwareStatus() const;
  void               requestStatusEmit();

  // void       setWorkStatus(WorkStatus const status, bool const toLog = true);

signals:
  void statusChanged(HardwareStatusEnum newStatus, HardwareStatusEnum oldStatus);

private:
  void                    changeStatus(HardwareStatusEnum const &newStatus);
  void                    onSystemValueChanged(float value);
  // HardwareStatus changes should be only observed in ONLINE global state.
  ResHolder              *m_resHolder;
  bool                    m_enable;
  SystemValue            *m_statusSysVal;
  HardwareStatusEnum      m_status;
  QMetaObject::Connection m_updateSysValConnection;
};

inline HardwareStatus::HardwareStatusEnum HardwareStatus::getHardwareStatus() const {
  return m_status;
}
} // namespace deepvaccontrol

#endif // HARDWARESTATUS_H
