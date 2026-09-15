#ifndef PROGRAMGRAPH_H
#define PROGRAMGRAPH_H

#include <QFrame>

#include "programmodel.h"
#include "qcustomplot.h"

namespace deepvaccontrol {
class ProgramGraph : public QFrame {
  Q_OBJECT
public:
  explicit ProgramGraph(QWidget *parent = nullptr);
  void setModel(ProgramModel *newModel);
  void setGraphUpdatesEnabled(bool enable); // to preven multiple reDraw calls
  void selectInterval(int index);

signals:
  void intervalDoubleClicked(int);

private:
  void initGui();
  void reDraw();
  void calculateChartData();
  void handleMouseClicked(QMouseEvent *event);
  void handleMouseDoubleClicked(QMouseEvent *event);
  int  getIntervalIndexOnPlot(QPoint onScreenPosition);

  ProgramModel          *m_model;
  QStringList            m_paramOrder;
  QVector<int>           m_paramTypes; // type of each param
  QList<QVector<double>> m_chartData;

  QCustomPlot                      *m_plot;
  QSharedPointer<QCPAxisTickerText> ticker;
  QCPItemRect                      *selectionRect;

  int m_flagsParamIndexInModel;
  int m_flagsParamType;

  // int m_lastSelectedIndex;

  // bool m_redrawEnabled;
};
} // namespace deepvaccontrol

#endif // PROGRAMGRAPH_H
