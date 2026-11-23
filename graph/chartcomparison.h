#ifndef CHARTCOMPARISON_H
#define CHARTCOMPARISON_H

#include <QStringList>
#include <QVector>

#include "experimentlibrary.h"

namespace deepvaccontrol {

// What a comparison tab of the Analysis page shows: the same channels of several experiments, aligned on a common time.
struct ChartComparison {
  struct Member {
    QString       key;   // the tab it was chosen from ("live", "db:…", "day:…", a library key) or the library key
    QString       label; // short (legend)
    ExperimentRun run;   // how to read it (and to report it)
  };
  QVector<Member> members; // the first one is the reference
  QStringList     channels; // state names
  int             alignMode{0}; // core::AlignMode
  int             alignN{1};    // n-th setpoint change / crossing / stage
};

}

#endif // CHARTCOMPARISON_H
