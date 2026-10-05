#ifndef ANALYSISTOOLS_H
#define ANALYSISTOOLS_H

#include <QHash>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

#include "experimentanalysis.h"
#include "experimentlibrary.h"
#include "toolregistry.h"

namespace deepvaccontrol {
class AnalysisNotesStore;
}

namespace deepvaccontrol::assistant {

// What the tools may ask of the user interface; all optional.
struct AnalysisViewer {
  // open the run in Data analysis (channels by state name, time range in elapsed s, NaN = whole run);
  // returns an error text or empty
  std::function<QString(ExperimentRun const &, QStringList const &, double, double, QVector<ExperimentRun> const &)> show;
  // write an experiment report into the export folder; returns an error text or empty, path set on success
  std::function<QString(ExperimentRun const &, QVector<ExperimentRun> const &, bool pdf, QString &path)> report;
};

/**
 * @brief Read-only tools over the experiment library and the Analysis Core, for the assistant.
 *
 * They only see recorded data (ExperimentLibrary / the experiment database) and the notes: there is
 * no path from here to the PLC, the chamber settings, the PID or experiment execution. All numbers
 * come from core::analyseExperiment and friends, never from the model.
 *
 * Runs are numbered #1, #2, ... by start time (oldest first) across recorded files and the database,
 * and can also be referred to by title, database key, "latest" / "previous".
 */
class AnalysisTools {
public:
  AnalysisTools(ExperimentLibrary *library, AnalysisNotesStore *notes, AnalysisViewer viewer = {});

  void registerTools(ToolRegistry &registry);

  [[nodiscard]] QVector<ExperimentRun>       runs() const; // oldest first, index + 1 = run number
  [[nodiscard]] std::optional<ExperimentRun> resolve(QString const &reference, QString &error) const;
  [[nodiscard]] QString                      runId(ExperimentRun const &run) const; // "#N"

  // tool implementations (public for tests)
  QJsonObject listRuns(QJsonObject const &args) const;
  QJsonObject runSummary(QJsonObject const &args);
  QJsonObject listChannels(QJsonObject const &args);
  QJsonObject statistics(QJsonObject const &args);
  QJsonObject kpis(QJsonObject const &args);
  QJsonObject stages(QJsonObject const &args);
  QJsonObject anomalies(QJsonObject const &args);
  QJsonObject compareRuns(QJsonObject const &args);
  QJsonObject rankRuns(QJsonObject const &args) const;
  QJsonObject runDetails(QJsonObject const &args);
  QJsonObject events(QJsonObject const &args);
  QJsonObject plotRun(QJsonObject const &args);
  QJsonObject generateReport(QJsonObject const &args);

private:
  struct Loaded {
    ExperimentRun                         run;
    std::shared_ptr<ExperimentData const> data;
    QStringList                           names; // channel name of each data.states column
    QVector<ChannelInfo>                  channels;
    QVector<double>                       times; // elapsed s
    core::ExperimentAnalysis              analysis;

    [[nodiscard]] QVector<double> series(QString const &name) const;
  };
  using LoadedPtr = std::shared_ptr<Loaded const>;

  LoadedPtr load(QString const &reference, QString &error);
  LoadedPtr load(ExperimentRun const &run, QString &error);
  // stage index from "3", "last", "last_hold", "hold:2", "ramp:-1", "final stabilization", ...; -1 + error
  static int selectStage(core::ExperimentAnalysis const &analysis, QString const &selector, QString &error);
  QJsonObject runHeader(Loaded const &loaded) const;

  ExperimentLibrary  *m_library;
  AnalysisNotesStore *m_notes;
  AnalysisViewer      m_viewer;
  QHash<QString, LoadedPtr> m_cache;
  QStringList               m_cacheOrder; // most recent last
};

} // namespace deepvaccontrol::assistant

#endif // ANALYSISTOOLS_H
