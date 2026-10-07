#ifndef DATAANALYSISWIDGET_H
#define DATAANALYSISWIDGET_H

#include <QWidget>

#include "experimentlibrary.h"

class QLineEdit;
class QFrame;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace deepvaccontrol {

class AnalysisEditorArea;
class AnalysisNotesStore;
struct ResHolder;

/**
 * @brief "Data analysis" menu page: browser of previous experiments found in the recorded
 * data and a tabbed / split area to analyse and compare them (with synced crosshair).
 */
class DataAnalysisWidget : public QWidget {
  Q_OBJECT
public:
  explicit DataAnalysisWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  // shared with the assistant (read-only analysis tools)
  [[nodiscard]] ExperimentLibrary  *library() const;
  [[nodiscard]] AnalysisNotesStore *notes() const;
  // scans the recorded experiments if the page has never been shown
  void ensureScanned();
  // opens (or focuses) the run's tab with the given view; returns an error text or empty
  QString showRun(ExperimentRun const &run, QStringList const &channels, double from, double to,
                  QVector<ExperimentRun> const &compare);
  // writes an experiment report of the run (and comparison) into the export folder
  QString writeReport(ExperimentRun const &run, QVector<ExperimentRun> const &compare, bool pdf, QString &path);

protected:
  void showEvent(QShowEvent *event) override;

private:
  void populateTree();
  void applyFilter();
  void openRun(ExperimentRun const &run);
  void openItem(QTreeWidgetItem *item);
  void openCustomRange();
  QDate selectedDay() const;

  ResHolder          *m_resHolder;
  ExperimentLibrary  *m_library;
  AnalysisNotesStore *m_notes;
  AnalysisEditorArea *m_editor;

  QFrame      *m_browser{nullptr};
  QTreeWidget *m_tree{nullptr};
  QLineEdit   *m_filterEdit{nullptr};
  QPushButton *m_refreshBtn{nullptr};
  bool         m_scanned{false};
};

} // namespace deepvaccontrol

#endif // DATAANALYSISWIDGET_H
