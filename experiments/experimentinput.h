#ifndef EXPERIMENTINPUT_H
#define EXPERIMENTINPUT_H

#include <QColor>
#include <QString>
#include <QStringList>

#include "analysisnotesstore.h"
#include "experimentanalysis.h"
#include "experimentlibrary.h"

namespace deepvaccontrol {

// Recorded channels of an experiment (names: one per data.states column, as ExperimentLibrary::channels
// reports them) plus its variable rules, as the input of core::analyseExperiment. Times are elapsed s.
core::ExperimentInput experimentInput(ExperimentData const &data, QStringList const &channelNames,
                                      QVector<AnalysisNotesStore::Rule> const &rules = {});

// Shared wording / colours of the analysis results (run page, comparison panel, report).
namespace analysistext {
QString stageName(core::Segment::Kind kind);
QColor  stageColor(core::Segment::Kind kind);
QString anomalyName(core::Anomaly::Kind kind);
QString severityName(core::Anomaly::Severity severity);
QColor  severityColor(core::Anomaly::Severity severity);
QString number(double value, int precision = 4); // "—" for NaN
QString seconds(double seconds);                 // h:mm:ss, "—" for NaN
QString percent(double fraction);                // 0 ... 1 -> "12.3 %"
QString pidShares(core::PidContribution const &pid); // "9 / 91 / 0 %"
}

}

#endif // EXPERIMENTINPUT_H
