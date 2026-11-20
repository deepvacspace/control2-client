#ifndef PROGRAMGRAPH_H
#define PROGRAMGRAPH_H

#include <QFrame>
#include <optional>

#include "programmodel.h"
#include "qcustomplot.h"

namespace deepvaccontrol {
class ProgramGraph : public QFrame {
  Q_OBJECT
public:
  explicit ProgramGraph(QWidget *parent = nullptr);
  // the chamber's pressure channel label ("Pressure, mbar"): a programmed pressure is in its unit, drawn in Torr
  void setPressureLabel(QString const &label);
  // the chamber's temperature now (none: not known): where a ramp in the first interval is drawn from
  void setStartTemperature(std::optional<double> temperature);
  void setModel(ProgramModel *newModel);
  void setGraphUpdatesEnabled(bool enable); // to preven multiple reDraw calls
  void selectInterval(int index);

signals:
  void intervalDoubleClicked(int);

protected:
  void showEvent(QShowEvent *event) override; // drawn again when the start moved while hidden

private:
  void initGui();
  void reDraw();
  void calculateChartData();
  void handleMouseClicked(QMouseEvent *event);
  void handleMouseDoubleClicked(QMouseEvent *event);
  int  getIntervalIndexOnPlot(QPoint onScreenPosition);

  ProgramModel          *m_model{nullptr};
  QStringList            m_paramOrder;
  QVector<int>           m_paramTypes; // type of each param
  QList<QVector<double>> m_chartData;

  QCustomPlot                      *m_plot;
  QSharedPointer<QCPAxisTickerText> ticker;
  QCPItemRect                      *selectionRect;

  QString m_pressureLabel;
  std::optional<double> m_startTemp;
  bool                  m_stale{false}; // the start moved while hidden

  int m_flagsParamIndexInModel;
  int m_flagsParamType;

  // int m_lastSelectedIndex;

  // bool m_redrawEnabled;
};
}

#endif // PROGRAMGRAPH_H
