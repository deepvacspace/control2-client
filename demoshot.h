#ifndef DEMOSHOT_H
#define DEMOSHOT_H

namespace deepvaccontrol {
class DataManager;
class SystemValueList;

namespace demo {
// --demo-shot: an example run into the Live data, ending now (4 h: a temperature hold at 50 °C under vacuum, then the
// chamber idle, warming up to atmosphere slowly), the last sample as the chamber's values. Only for screenshots: run
// from a scratch working directory, the day's recording is written under it (data/recordings).
void fillExampleRun(SystemValueList *states, DataManager *dataManager);
}
}

#endif // DEMOSHOT_H
