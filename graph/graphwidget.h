#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "dataviewer.h"
#include "graphstyleeditor.h"
#include "resholder.h"
#include "runstore.h"

namespace deepvaccontrol {
class GraphWidget : public QWidget {
  Q_OBJECT
public:
  explicit GraphWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  void updateModeSelectButton(DataViewer::ControlMode mode);
  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);

private:
  void              exportData();
  void              updateDayButton();
  // legend box over the chart: line labels only, or labels with the values under the mouse
  void              updateLegendMode();
  // the calendar: experiments of the experiment database, or the live data of today
  void              openCalendar();
  void              showExperiment(storage::ExperimentSpan const &experiment);
  void              showLive();
  QPushButton      *m_labelsBtn{nullptr};
  QPushButton      *m_valuesBtn{nullptr};
  QPushButton      *m_dayBtn{nullptr};
  QString           m_experimentName; // experiment shown; empty for the live data
  bool              m_loading{false};
  ResHolder        *m_resHolder;
  DataViewer       *m_dataViewer;
  GraphStyleEditor *m_styleEditor;
  QPushButton      *m_modeSelectBtn;
  QPushButton      *m_exportToBtn;
  QPushButton      *m_styleBtn;
};
} // namespace deepvaccontrol

#endif // GRAPHWIDGET_H
