#ifndef RESHOLDER_H
#define RESHOLDER_H

#include "accessmanager.h"
#include "actionmanager.h"
#include "datamanager.h"
#include "gatetcp.h"
#include "hardwarestatus.h" // hardware status
#include "logger.h"
#include "programmodel.h"
#include "settingsmanager.h"
#include "statusmanager.h" // application status
#include "systemvaluelist.h"
#include "usbmanager.h"

namespace deepvaccontrol {

struct ResHolder {
  SystemValueList *states{nullptr};
  GateTcp         *tcpGate{nullptr};
  SettingsManager *settings{nullptr};
  ProgramModel    *defaultProgram{nullptr};
  Logger          *logger{nullptr};
  DataManager     *dataManager{nullptr};
  StatusManager   *status{nullptr};
  ActionManager   *actions{nullptr};
  AccessManager   *users{nullptr};
  HardwareStatus  *hard_status{nullptr};
  UsbManager      *usb{nullptr};
  bool             aiAdvisorEnabled{true};
};
} // namespace deepvaccontrol
#endif // RESHOLDER_H
