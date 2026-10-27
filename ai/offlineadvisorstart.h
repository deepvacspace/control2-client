#ifndef OFFLINEADVISORSTART_H
#define OFFLINEADVISORSTART_H

#include <QVector>

#include "programforecast.h"

class QWidget;

namespace deepvaccontrol {
class ProgramModel;
struct ResHolder;

// The program, the chamber's temperature and its Temperature PID table, for the digital twin (error: why not).
ai::ForecastInput forecastInputFromProgram(ResHolder *resHolder, ProgramModel *program);

// Returns true only when the caller should send the experiment to the PLC: started without the advisor, or its
// recommended settings applied with "Apply settings and start". "Apply settings" alone leaves it stopped.
bool offerOfflineAdvisorBeforeStart(ResHolder *resHolder, ProgramModel *program, QWidget *parent);

// Program control: the AI Advisor plans each temperature interval on its own (from where the one before is predicted
// to end). True when the caller should send the program; *plan then holds the settings per interval for
// ProgramPlanRunner, or is empty when started without the advisor. ready: a plan already calculated in the background,
// used when it was made for this program, these settings and about this chamber temperature.
bool offerProgramPlanBeforeStart(ResHolder *resHolder, ProgramModel *program, QWidget *parent, QVector<ai::IntervalPlan> *plan,
                                 ai::ProgramPlan const *ready = nullptr);

}

#endif // OFFLINEADVISORSTART_H
