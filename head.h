#ifndef HEAD_H
#define HEAD_H

#include <QVector>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "resholder.h"
#include "stopbutton.h"

class QStackedWidget;

namespace deepvaccontrol {
class ChamberView;
class GraphStyleEditor;
class HomeTrend;
class ControlBar;

// Home page: chamber status and temperature, test time and program position, the components and the experiment chart.
class Head : public QWidget {
  Q_OBJECT
public:
  explicit Head(ResHolder *resHolder, QWidget *parent = nullptr);

signals:

public:

  // Lazy setup all type of connection between components
  void setupGuiAndLogic();

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override; // keeps the corner buttons and the Channels panel in place

private:
  void       handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);
  void       initBaseGui();
  void       restyle();
  void       placeOverlays();
  void       setChannelsOpen(bool open);
  void       updateGui();
  ResHolder *m_resHolder;

  // the left side: the chart (last 2 hours, or the running experiment) or the chamber's diagram
  QStackedWidget *m_leftStack{nullptr};
  HomeTrend      *m_trend{nullptr};
  ChamberView    *m_diagram{nullptr};
  QWidget        *m_corner{nullptr};      // the buttons in the top right corner of the left side
  QPushButton    *m_channelsBtn{nullptr}; // the chart's Channels panel (with the chart only)
  QPushButton    *m_viewSwitch{nullptr};  // "Diagram" / "Chart": the other view
  QFrame         *m_channelsPanel{nullptr}; // over the chart, under the corner buttons: as the Analysis page's
  QLabel         *m_channelsCount{nullptr};
  GraphStyleEditor *m_channels{nullptr};
  QLabel      *m_statusDot{nullptr};
  QLabel      *m_statusText{nullptr};
  QLabel      *m_tempValue{nullptr};
  QLabel      *m_tempUnit{nullptr};
  QLabel      *m_presValue{nullptr}; // in Torr; a dash without a gauge or a connection
  QLabel      *m_presUnit{nullptr};
  QLabel      *m_setpointValue{nullptr}; // temperature
  QLabel      *m_outputValue{nullptr};
  ControlBar  *m_outputBar{nullptr};
  QLabel      *m_presSetpointValue{nullptr}; // pressure
  QLabel      *m_presOutputValue{nullptr};
  ControlBar  *m_presOutputBar{nullptr};
  QLabel      *m_timeTitle{nullptr};
  QLabel      *m_curWorkTimeLabel{nullptr};
  QLabel      *m_intervalLabel{nullptr};
  QLabel      *m_cycleLabel{nullptr};
  QWidget     *m_intervalBox{nullptr};
  QWidget     *m_cycleBox{nullptr};

  StopButton  *m_stopBtn;
  QPushButton *m_nextIntervalBtn;

  AccessManager::AccessLevel m_accessLevel;
};
}

#endif // HEAD_H
