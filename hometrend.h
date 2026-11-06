#ifndef HOMETREND_H
#define HOMETREND_H

#include <QFrame>
#include <QMap>
#include <QVariantList>

class QCPAxis;
class QCPGraph;
class QCustomPlot;
class QHBoxLayout;
class QLabel;
class QTimer;

namespace deepvaccontrol {
struct ResHolder;

/**
 * @brief Home page: the chosen channels of the current experiment, from its start (or the last 2 hours when no job
 * runs), following the live data. Temperatures on the left axis; the pressure and its setpoint in Torr on a
 * logarithmic axis at the right; any other unit on a second axis at the right. Setpoints only while a job runs.
 */
class HomeTrend : public QFrame {
  Q_OBJECT
public:
  explicit HomeTrend(ResHolder *resHolder, QWidget *parent = nullptr);

  void restyle();
  // the channels' styles (settings "states" order): shown or not, colour, line style and width
  void setStyles(QVariantList const &styles);
  void setStyle(int index, QVariantMap const &style);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void refresh();
  void applyStyle(int index);

  ResHolder              *m_resHolder;
  QCustomPlot            *m_plot;
  QCPAxis                *m_otherAxis{nullptr}; // neither temperature nor pressure
  QVariantList            m_styles;
  QMap<int, QCPGraph *>   m_graphs; // by channel (state) index
  QLabel      *m_title;
  QTimer      *m_timer;
};

}

#endif // HOMETREND_H
