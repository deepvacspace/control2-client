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

  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);

private:
  void              exportData();
  void              updateDayButton();
  // labels box over the chart and value label at the mouse, independent of each other
  void              updateLegendMode();
  // the calendar: experiments of the experiment database, or the live data of today
  void              openCalendar();
  void              showExperiment(storage::ExperimentSpan const &experiment);
  void              showDay(QDate const &day, QVector<storage::ExperimentSpan> const &experiments);
  void              showLive();
  QPushButton      *m_labelsBtn{nullptr};
  QPushButton      *m_valuesBtn{nullptr};
  QPushButton      *m_dayBtn{nullptr};
  QString           m_experimentName;  // experiment shown; empty for the live data
  QString           m_experimentLabel; // its short text on the day button
  bool              m_loading{false};
  ResHolder        *m_resHolder;
  DataViewer       *m_dataViewer;
  GraphStyleEditor *m_styleEditor;
  QPushButton      *m_exportToBtn;
  QPushButton      *m_styleBtn;
};
} // namespace deepvaccontrol

#endif // GRAPHWIDGET_H
