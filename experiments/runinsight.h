#ifndef RUNINSIGHT_H
#define RUNINSIGHT_H

#include <QVariantList>
#include <memory>

#include "analysisnotesstore.h"
#include "experimentanalysis.h"
#include "experimentlibrary.h"
#include "runmanifest.h"

namespace deepvaccontrol {

// What the analysis core finds in one experiment, and its events: the KPIs, stages, anomalies and event views of
// the Analysis page. Analysis times are elapsed from `start`; event times are epoch s.
struct RunInsight {
  std::shared_ptr<core::ExperimentAnalysis const> analysis;
  QVector<core::RunEvent>                         events;
  bool                                            eventsRecorded{false}; // from the run's manifest, else derived from the signals
  double                                          start{0.0};            // epoch s of the first sample
  qint64                                          samples{0};
};

namespace runinsight {
// samples: times (epoch s) and the channels in the order of `names` (state names). Thread safe: reads the experiment
// database (manifest of runs saved there) but uses no objects of the application.
RunInsight compute(ExperimentRun const &run, QVector<double> const &times, QVector<QVector<double>> const &states,
                   QStringList const &names, QVector<AnalysisNotesStore::Rule> const &rules, QVariantList const &alarmsConfig);
}

}

#endif // RUNINSIGHT_H
