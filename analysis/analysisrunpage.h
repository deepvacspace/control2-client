#ifndef ANALYSISRUNPAGE_H
#define ANALYSISRUNPAGE_H

#include <QHash>
#include <QWidget>
#include <memory>

#include "accessmanager.h"
#include "analysischart.h"
#include "experimentlibrary.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace deepvaccontrol {

class AnalysisNotesStore;
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

private:
  void buildUi();
  QWidget *buildNotesPanel();
  void     load();
  void     refreshChart();
  void     refreshRegions();
  void     refreshBands();
  void     refreshEvents();
  void     refreshStats();
  void     refreshRulesList();
  void     refreshAnnotationsList();
  void     updateTimeControls();
  void     updateChannelsButton();
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
  void askExportPath(QString const &extension, std::function<bool(QString const &)> writer);

  [[nodiscard]] bool              compareMode() const;
  [[nodiscard]] int               compareChannel() const;
  [[nodiscard]] QColor            channelColor(int index) const;
  [[nodiscard]] QVector<double>   elapsed(ExperimentData const &data) const;

  ResHolder          *m_resHolder;
  ExperimentLibrary  *m_library;
  AnalysisNotesStore *m_notes;
  ExperimentRun       m_run;

  std::shared_ptr<ExperimentData const>                  m_data;
  QVector<double>                                        m_elapsed;
  QVector<ChannelInfo>                                   m_channels;
  QVector<int>                                           m_selected; // sorted channel indexes
  QHash<int, QColor>                                     m_colorOverrides;
  QStringList                                            m_compareKeys;
  QHash<QString, std::shared_ptr<ExperimentData const>> m_compareData;

  AnalysisChart  *m_chart{nullptr};
  QLabel         *m_titleLabel{nullptr};
  QLabel         *m_statsLabel{nullptr};
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
  QWidget        *m_notesPanel{nullptr};
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
