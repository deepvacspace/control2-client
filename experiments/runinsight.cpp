#include "runinsight.h"

#include "experimentinput.h"
#include "runstore.h"

namespace deepvaccontrol::runinsight {

RunInsight compute(ExperimentRun const &run, QVector<double> const &times, QVector<QVector<double>> const &states,
                   QStringList const &names, QVector<AnalysisNotesStore::Rule> const &rules, QVariantList const &alarmsConfig) {
  RunInsight insight;
  if (times.isEmpty()) {
    return insight;
  }
  insight.start   = times.first();
  insight.samples = times.size();

  ExperimentData data;
  data.run          = run;
  data.times        = times;
  data.states       = states;
  data.channelNames = names;
  insight.analysis  = std::make_shared<core::ExperimentAnalysis const>(core::analyseExperiment(experimentInput(data, names, rules)));

  // the events recorded with a saved experiment, else those visible in the signals
  if (!run.storeKey.isEmpty()) {
    if (auto const manifest = storage::RunStore::manifest(run.storeKey); manifest && manifest->isValid()) {
      insight.events         = manifest->events;
      insight.eventsRecorded = true;
    }
  }
  if (!insight.eventsRecorded) {
    QHash<QString, QVector<double>> channels;
    for (qsizetype c{0}; (c < names.size()) && (c < states.size()); ++c) {
      channels.insert(names[c], states[c]);
    }
    insight.events = core::detectEvents(times, channels, core::alarmLabels(alarmsConfig));
  }
  return insight;
}

} // namespace deepvaccontrol::runinsight
