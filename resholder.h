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

class ExperimentRecorder;
class CollabManager;
class AnalysisServices;

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
  UsbManager           *usb{nullptr};
  ExperimentRecorder   *recorder{nullptr};
  CollabManager        *collab{nullptr};
  AnalysisServices     *analysis{nullptr}; // experiment library, notes, reports: shared by the pages
  bool                  aiAdvisorEnabled{true};
};
}
#endif // RESHOLDER_H
