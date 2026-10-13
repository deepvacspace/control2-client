#include "analysisservices.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <memory>

#include "analysischart.h"
#include "analysisnotesstore.h"
#include "collabmanager.h"
#include "comparisonpanel.h"
#include "constants.h"
#include "experimentinput.h"
#include "experimentrecorder.h"
#include "experimentreport.h"
#include "resholder.h"
#include "runmanifest.h"
#include "runstore.h"

namespace deepvaccontrol {

AnalysisServices::AnalysisServices(ResHolder *resHolder, QObject *parent)
    : QObject{parent}
    , m_resHolder{resHolder}
    , m_library{new ExperimentLibrary(deepvaccontrol::INTERVALS_DIR,
                                      deepvaccontrol::ACTIONS_FILE,
                                      [resHolder]() {
                                        return resHolder->settings->settingsMap()["states"].toList();
                                      },
                                      this)}
    , m_notes{new AnalysisNotesStore(deepvaccontrol::ANALYSIS_NOTES_FILE, this)} {
  // new experiments: saved here, or received from a paired computer
  if (m_resHolder->recorder) {
    connect(m_resHolder->recorder, &ExperimentRecorder::runSaved, m_library, &ExperimentLibrary::refresh);
  }
  if (m_resHolder->collab) {
    connect(m_resHolder->collab, &CollabManager::localDataChanged, this, [this] {
      m_notes->notifyExternalChange();
      m_library->refresh();
    });
  }
}

void AnalysisServices::ensureScanned() {
  if (!m_library->hasScanned()) {
    m_library->refresh();
  }
}

void AnalysisServices::setShowHandler(ShowHandler handler) {
  m_show = std::move(handler);
}

QString AnalysisServices::show(ExperimentRun const &run, QStringList const &channels, double from, double to,
                               QVector<ExperimentRun> const &compare) {
  return m_show ? m_show(run, channels, from, to, compare) : tr("No page can show experiments");
}

QString AnalysisServices::writeReport(ExperimentRun const &run, QVector<ExperimentRun> const &compare, bool pdf, QString &path,
                                      QStringList const &channels) {
  // one experiment, read and analysed as the analysis pages do
  struct Loaded {
    std::shared_ptr<ExperimentData const> data;
    QVector<ChannelInfo>                  infos;
    QStringList                           names;
    QVector<double>                       elapsed;
    core::ExperimentAnalysis              analysis;

    [[nodiscard]] QVector<double> series(QString const &name) const {
      qsizetype const index{names.indexOf(name)};
      return ((index >= 0) && (index < data->states.size())) ? data->states[index] : QVector<double>{};
    }
  };
  auto load = [this](ExperimentRun const &experiment, bool ownRules, QString &error) -> std::shared_ptr<Loaded> {
    auto loaded  = std::make_shared<Loaded>();
    loaded->data = m_library->load(experiment, &error);
    if (!loaded->data || loaded->data->times.isEmpty()) {
      if (error.isEmpty()) {
        error = tr("Could not read %1").arg(experiment.title);
      }
      return nullptr;
    }
    loaded->infos = m_library->channels(*loaded->data);
    for (ChannelInfo const &info : std::as_const(loaded->infos)) {
      loaded->names << info.name;
    }
    double const start{loaded->data->times.first()};
    for (double const time : loaded->data->times) {
      loaded->elapsed.push_back(time - start);
    }
    // rules are per run: a run's own rules apply to it only
    loaded->analysis = core::analyseExperiment(experimentInput(*loaded->data, loaded->names,
                                                               ownRules ? m_notes->rules(experiment.key) : QVector<AnalysisNotesStore::Rule>{}));
    return loaded;
  };

  QString    error;
  auto const main = load(run, true, error);
  if (!main) {
    return error;
  }

  ExperimentReportData report;
  report.run         = main->data->run;
  report.analysis    = &main->analysis;
  report.events      = m_library->events(report.run.start, report.run.end);
  report.annotations = m_notes->annotations(report.run.start, report.run.end, run.key);
  report.rules       = m_notes->rules(run.key);
  report.author      = m_resHolder->users ? m_resHolder->users->getCurrentUserName() : QString{};

  // the self-describing record: its manifest, or the events visible in the signals for runs without one
  core::RunManifest manifest = core::RunManifest::fromJson(main->data->manifest);
  report.manifestStored      = manifest.isValid();
  report.integrity           = 2;
  if (report.manifestStored) {
    report.integrity = static_cast<int>(storage::RunStore::verify(run.storeKey));
  } else {
    manifest = core::RunManifest{};
    QHash<QString, QVector<double>> recorded;
    for (qsizetype c{0}; (c < main->names.size()) && (c < main->data->states.size()); ++c) {
      recorded.insert(main->names[c], main->data->states[c]);
    }
    manifest.events   = core::detectEvents(main->data->times, recorded,
                                           core::alarmLabels(m_resHolder->settings->settingsMap().value("alarms").toList()));
    manifest.channels = core::channelMetadata(m_resHolder->settings->settingsMap().value("states").toList(), main->names);
  }
  report.manifest = manifest.toJson();

  int const width{1600};
  int const height{700};
  auto      chartOf = [&](QVector<ChartSeries> const &series, QVector<ChartRegion> const &stages, QVector<ChartEvent> const &markers) {
    AnalysisChart chart;
    chart.resize(width, height);
    chart.beginUpdate();
    chart.setSeries(series, report.run.start);
    chart.setStages(stages);
    chart.setEvents(markers);
    chart.endUpdate();
    chart.resetView();
    return chart.renderImage(width, height);
  };
  auto seriesOf = [&](QStringList const &names) {
    QVector<ChartSeries> series;
    for (QString const &name : names) {
      qsizetype const index{main->names.indexOf(name)};
      if ((index < 0) || (index >= main->data->states.size())) {
        continue;
      }
      ChartSeries item;
      item.label   = main->infos[index].label;
      item.color   = main->infos[index].color;
      item.penStyle = main->infos[index].penStyle;
      item.logAxis = main->infos[index].scientific;
      item.x       = main->elapsed;
      item.y       = main->data->states[index];
      series.push_back(item);
    }
    return series;
  };

  // comparison: the other runs, aligned on the comparison's settings, with the differences as HTML
  QVector<std::shared_ptr<Loaded>> compared;
  for (ExperimentRun const &other : compare) {
    QString otherError;
    if (auto loaded = load(other, false, otherError)) {
      compared.push_back(loaded);
    }
  }
  QString const signal = channels.isEmpty() ? QStringLiteral("temp") : channels.first();
  if (!compared.isEmpty()) {
    QVector<ComparedRun> runs;
    auto add = [&](Loaded const &loaded) {
      ComparedRun item;
      item.title     = loaded.data->run.title;
      item.run       = loaded.data->run;
      item.analysis  = &loaded.analysis;
      item.times     = loaded.elapsed;
      item.temp      = loaded.series("temp");
      item.reference = loaded.series("temp_ref");
      item.signal    = loaded.series(signal);
      item.manifest  = loaded.data->manifest;
      runs.push_back(item);
    };
    add(*main);
    for (auto const &loaded : std::as_const(compared)) {
      add(*loaded);
    }
    qsizetype const signalIndex{main->names.indexOf(signal)};
    QString const   signalLabel = (signalIndex >= 0) ? main->infos[signalIndex].label : signal;
    ComparisonPanel panel;
    panel.setRuns(runs, signalLabel);
    report.comparisonHtml          = panel.html();
    QVector<double> const offsets  = panel.offsets();
    QVector<QColor> const palette{QColor("#2563eb"), QColor("#dc2626"), QColor("#16a34a"), QColor("#9333ea"),
                                  QColor("#d97706"), QColor("#0d9488"), QColor("#db2777"), QColor("#475569")};
    QVector<ChartSeries> series;
    for (int k{0}; k < runs.size(); ++k) {
      ChartSeries item;
      item.label = runs[k].title;
      item.color = palette[k % palette.size()];
      item.x     = runs[k].times;
      for (double &time : item.x) {
        time += offsets.value(k);
      }
      item.y = runs[k].signal;
      series.push_back(item);
    }
    report.charts.push_back({tr("Comparison of %1, aligned on %2").arg(signalLabel, panel.alignmentText()), chartOf(series, {}, {})});
  } else {
    QStringList shown = channels;
    if (shown.isEmpty()) {
      for (ChannelInfo const &info : std::as_const(main->infos)) {
        if (info.defaultVisible) {
          shown << info.name;
        }
      }
    }
    report.charts.push_back({tr("Chart"), chartOf(seriesOf(shown), {}, {})});
  }

  // process and control charts with the stages and anomalies
  QVector<ChartRegion> stages;
  for (core::StageAnalysis const &stage : std::as_const(main->analysis.run.stages)) {
    core::Segment const &segment = stage.segment;
    stages.push_back({segment.start, std::max(segment.end, segment.start + 1.0), analysistext::stageName(segment.kind),
                      analysistext::stageColor(segment.kind)});
  }
  QVector<ChartEvent> markers;
  for (core::Anomaly const &anomaly : main->analysis.anomalies) {
    if (anomaly.severity >= core::Anomaly::Severity::WARNING) {
      markers.push_back({anomaly.start, analysistext::anomalyName(anomaly.kind), analysistext::severityColor(anomaly.severity)});
    }
  }
  for (auto const &[names, caption] : {std::pair{QStringList{"temp", "temp_ref"}, tr("Temperature and setpoint with stages and anomalies (warning or worse)")},
                                       std::pair{QStringList{"temp_u", "temp_u_p", "temp_u_i", "temp_u_d"}, tr("Control output and its P / I / D terms")}}) {
    QVector<ChartSeries> const series = seriesOf(names);
    if (!series.isEmpty()) {
      report.charts.push_back({caption, chartOf(series, stages, markers)});
    }
  }

  // into the export folder
  QString name = run.title;
  name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]+")), QStringLiteral("_"));
  QDir const folder(QDir(deepvaccontrol::EXPORT_DIR).absoluteFilePath(QStringLiteral("reports")));
  QDir().mkpath(folder.absolutePath());
  path = folder.filePath(QStringLiteral("%1_%2.%3").arg(name, QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"), pdf ? "pdf" : "html"));
  bool const ok{pdf ? experimentreport::writePdf(path, report) : experimentreport::writeHtml(path, report)};
  return ok ? QString{} : tr("Could not write %1").arg(path);
}

} // namespace deepvaccontrol
