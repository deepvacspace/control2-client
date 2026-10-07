#ifndef ANALYSISRUNPAGE_H
#define ANALYSISRUNPAGE_H

#include <QHash>
#include <QWidget>
#include <memory>

#include "accessmanager.h"
#include "analysischart.h"
#include "derivedvariablesstore.h"
#include "experimentlibrary.h"
#include "experimentanalysis.h"
#include "experimentreport.h"
#include "runmanifest.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextBrowser;

namespace deepvaccontrol {

class AnalysisNotesStore;
class ComparisonPanel;
class LineEditNum;
struct ResHolder;

/**
 * @brief Data analysis page of one experiment (or custom time range), shown as a tab.
 */
class AnalysisRunPage : public QWidget {
  Q_OBJECT
public:
  AnalysisRunPage(ResHolder          *resHolder,
                  ExperimentLibrary  *library,
                  AnalysisNotesStore *notes,
                  ExperimentRun       run,
                  QWidget            *parent = nullptr);

  [[nodiscard]] QString        key() const;
  [[nodiscard]] QString        title() const;
  [[nodiscard]] AnalysisChart *chart() const;

  // loads the run now if the page has not done it yet; false when it cannot be read
  bool ensureLoaded();
  // channels by state name (unknown ones ignored), time range in elapsed s (NaN = whole run),
  // other runs overlaid (compare mode); returns an error text or empty
  QString applyView(QStringList const &channels, double from, double to, QVector<ExperimentRun> const &compare);
  // configuration, stages, KPIs, events, anomalies, charts and the comparison (needs the run loaded)
  [[nodiscard]] ExperimentReportData buildReport();

private:
  void buildUi();
  QWidget *buildNotesPanel();
  void     load();
  void     refreshChart();
  void     refreshRegions();
  void     refreshBands();
  void     refreshEvents();
  // statistics panel: selected range (core::computeStats + core::analyseRun of the slice), run KPIs / stages
  // (core::analyseExperiment), anomalies and the comparison with other runs
  QWidget *buildStatisticsPanel();
  void     refreshRangeStatistics();
  void     refreshRangeKpis(double from, double to); // control KPIs + stages of the visible range (core::analyseRun)
  void     computeRunAnalysis();
  void     refreshStages();
  void     refreshAnomalies();
  // comparison panel / aligned compare chart
  void     refreshComparison();
  [[nodiscard]] core::ExperimentAnalysis analyse(ExperimentData const &data) const;
  [[nodiscard]] double          compareOffset(ExperimentData const &data) const;     // alignment shift of a compared run
  [[nodiscard]] QVector<double> comparedElapsed(ExperimentData const &data) const;   // elapsed + alignment shift
  void     exportReport(bool pdf);
  // self-describing run: manifest, typed events, run record, integrity
  void     loadRecord();
  void     refreshRecord();
  void     exportBundle();
  void     editRunDetails();
  [[nodiscard]] QString recordHtml() const;
  [[nodiscard]] int channelIndex(QString const &name) const;
  void     refreshStats();
  void     refreshRulesList();
  void     refreshAnnotationsList();
  void     updateTimeControls();
  void     updateChannelsButton();
  // derived (calculated) channels appended after the recorded ones
  void     applyDerived();
  void     openDerivedDialog();
  // compare mode reference line
  void     refreshSetpoint();
  void     updateSetpointDefault();
  void     handleAccessChange(QString const &name, AccessManager::AccessLevel level);

  void openChannelsMenu();
  void openCompareMenu();
  void setChannelSelected(int index, bool selected);
  void setSelectedChannels(QVector<int> selection);
  void setCompareRun(ExperimentRun const &run, bool compared);
  void onAnnotationDrawn(double x0, double x1);
  void addRule();

  void exportChartPng();
  void exportRunCsv();
  void exportComparisonCsv();
  void saveToDatabase();
  void askExportPath(QString const &extension, std::function<bool(QString const &)> writer);

  [[nodiscard]] bool              compareMode() const;
  [[nodiscard]] int               compareChannel() const;
  [[nodiscard]] QColor            channelColor(int index) const;
  [[nodiscard]] QVector<double>   elapsed(ExperimentData const &data) const;
  [[nodiscard]] int              channelIndexIn(ExperimentData const &data, int channel) const;
  // values of one of this page's channels in another experiment (derived ones are computed), empty if missing
  [[nodiscard]] QVector<double>  channelSeries(ExperimentData const &data, int channel) const;

  ResHolder          *m_resHolder;
  ExperimentLibrary  *m_library;
  AnalysisNotesStore *m_notes;
  ExperimentRun       m_run;

  std::shared_ptr<ExperimentData const>                  m_data;     // recorded + derived channels
  std::shared_ptr<ExperimentData const>                  m_baseData; // as loaded
  int                                                    m_baseChannelCount{0};
  QVector<storage::DerivedVariable>                      m_derived;  // channels after m_baseChannelCount
  int                                                    m_setpointDefaultFor{-1};
  QVector<double>                                        m_elapsed;
  QVector<ChannelInfo>                                   m_channels;
  QVector<int>                                           m_selected; // sorted channel indexes
  QHash<int, QColor>                                     m_colorOverrides;
  QStringList                                            m_compareKeys;
  QHash<QString, std::shared_ptr<ExperimentData const>> m_compareData;
  QHash<QString, core::ExperimentAnalysis>               m_compareAnalyses;
  QHash<QString, double>                                 m_compareOffsets; // run key -> alignment shift, s

  AnalysisChart  *m_chart{nullptr};
  QLabel         *m_titleLabel{nullptr};
  QLabel         *m_statsLabel{nullptr};
  QLabel         *m_integrityBadge{nullptr};
  QVector<QLabel *> m_kpiTiles; // MAE, RMSE, max error, overshoot, undershoot, settling, tail MAE
  QPushButton    *m_channelsBtn{nullptr};
  QPushButton    *m_compareBtn{nullptr};
  QComboBox      *m_modeCombo{nullptr};
  QPushButton    *m_annotateBtn{nullptr};
  QPushButton    *m_exportBtn{nullptr};
  QDoubleSpinBox *m_fromSpin{nullptr};
  QDoubleSpinBox *m_toSpin{nullptr};
  QSpinBox       *m_smoothSpin{nullptr};
  QCheckBox      *m_minCheck{nullptr};
  QCheckBox      *m_maxCheck{nullptr};
  QCheckBox      *m_avgCheck{nullptr};
  QCheckBox      *m_eventsCheck{nullptr};
  QWidget        *m_setpointGroup{nullptr};
  QCheckBox      *m_setpointCheck{nullptr};
  LineEditNum    *m_setpointEdit{nullptr};
  QWidget        *m_notesPanel{nullptr};
  QWidget        *m_statisticsPanel{nullptr};
  QLabel         *m_rangeLabel{nullptr};
  QTableWidget   *m_rangeTable{nullptr};
  QLabel         *m_rangeKpiLabel{nullptr};
  QLabel         *m_kpiLabel{nullptr};
  QTableWidget   *m_stagesTable{nullptr};
  QCheckBox      *m_stagesCheck{nullptr};
  QLabel         *m_anomalyLabel{nullptr};
  QTableWidget   *m_anomalyTable{nullptr};
  QCheckBox      *m_anomaliesCheck{nullptr};
  ComparisonPanel *m_comparisonPanel{nullptr};
  QTableWidget   *m_eventsTable{nullptr};
  QLabel         *m_eventsLabel{nullptr};
  QTextBrowser   *m_recordView{nullptr};
  core::RunManifest m_manifest;          // stored run's manifest; for other runs only derived events
  bool              m_manifestStored{false};
  int               m_integrity{2};      // storage::RunStore::Integrity
  QTimer         *m_statisticsTimer{nullptr};
  std::optional<core::ExperimentAnalysis> m_analysis;
  QComboBox      *m_ruleChannelCombo{nullptr};
  QLineEdit      *m_ruleLoEdit{nullptr};
  QLineEdit      *m_ruleHiEdit{nullptr};
  QLineEdit      *m_ruleLabelEdit{nullptr};
  QComboBox      *m_ruleColorCombo{nullptr};
  QListWidget    *m_rulesList{nullptr};
  QListWidget    *m_annotationsList{nullptr};
};

inline QString AnalysisRunPage::key() const {
  return m_run.key;
}

inline QString AnalysisRunPage::title() const {
  return m_run.title;
}

inline AnalysisChart *AnalysisRunPage::chart() const {
  return m_chart;
}

} // namespace deepvaccontrol

#endif // ANALYSISRUNPAGE_H
