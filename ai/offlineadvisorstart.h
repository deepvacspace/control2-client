#ifndef OFFLINEADVISORSTART_H
#define OFFLINEADVISORSTART_H

class QWidget;

namespace deepvaccontrol {
class ProgramModel;
struct ResHolder;

// Returns true only when the caller should send the experiment to the PLC.
// Selecting the advisor calculates and presents a recommendation, but leaves
// the experiment stopped for the PID to be changed manually in Settings.
bool offerOfflineAdvisorBeforeStart(ResHolder *resHolder, ProgramModel *program, QWidget *parent);

} // namespace deepvaccontrol

#endif // OFFLINEADVISORSTART_H
