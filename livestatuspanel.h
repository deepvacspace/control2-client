#ifndef LIVESTATUSPANEL_H
#define LIVESTATUSPANEL_H

#include <QElapsedTimer>
#include <QFrame>
#include <limits>

#include "resholder.h"

class QLabel;
class QPushButton;
class QTimer;

namespace deepvaccontrol {

class StopButton;
class StateIcon;

/**
 * @brief Analysis page, Live tab, right of the chart in the side column's card (above the options bar): the chamber's
 * status and,
 * beside it, the running experiment's name; three tiles (temperature, pressure, test time) sharing the height, each its
 * reading in a shaded box with an icon of what it is doing (heating, cooling, pumping, venting, standby; the clock) and
 * two details under it (control output and setpoint; the program's interval and cycle); then Stop and Next step. The
 * temperature's and the pressure's icons move with their control output, a small bar under them (StateIcon); the
 * readings in the monospace face.
 */
class LiveStatusPanel : public QFrame {
  Q_OBJECT
public:
  explicit LiveStatusPanel(ResHolder *resHolder, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  // a tile: the caption, its icon and the reading in a shaded box, two captioned details under it
  struct Tile {
    QLabel *caption{nullptr};
    StateIcon *icon{nullptr}; // top right of the shaded box
    QLabel *value{nullptr};
    QLabel *firstCaption{nullptr};
    QLabel *first{nullptr};
    QLabel *secondCaption{nullptr};
    QLabel *second{nullptr};
  };

  void initGui();
  void restyle();
  void updateGui();
  // withBar: the control output's bar under the icon (temperature, pressure)
  Tile addTile(QString const &caption, QString const &firstCaption, QString const &secondCaption, bool withBar);
  void showExperiment(); // its name, cut with "…" to the width beside the status
  void showTestTime();   // the chamber's test time, counted on here between its updates

  ResHolder   *m_resHolder;
  QLabel      *m_statusDot{nullptr};
  QLabel      *m_statusText{nullptr};
  QLabel      *m_experiment{nullptr};
  Tile         m_temp;
  Tile         m_pres; // in Torr; a dash without a gauge or a connection
  Tile         m_time; // test time; the program's interval and cycle
  StopButton  *m_stopBtn{nullptr};
  QPushButton *m_nextBtn{nullptr};
  QString      m_experimentText;
  // the test time: the chamber's last value, since when, the seconds shown; a second's tick
  double        m_totalBase{std::numeric_limits<double>::quiet_NaN()};
  QElapsedTimer m_totalSince;
  long          m_totalShown{-1};
  QTimer       *m_secondTick{nullptr};
};

}

#endif // LIVESTATUSPANEL_H
