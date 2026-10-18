#ifndef HOMETREND_H
#define HOMETREND_H

#include <QFrame>

class QCustomPlot;
class QHBoxLayout;
class QLabel;
class QTimer;

namespace deepvaccontrol {
struct ResHolder;

/**
 * @brief Home page: the temperature of the current experiment against its setpoint, from its start (or the last
 * 30 minutes when no job runs), following the live data.
 */
class HomeTrend : public QFrame {
  Q_OBJECT
public:
  explicit HomeTrend(ResHolder *resHolder, QWidget *parent = nullptr);

  void restyle();
  // shown beside the title (the plate and TMP markers)
  void addHeaderWidget(QWidget *widget);

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void refresh();

  ResHolder   *m_resHolder;
  QCustomPlot *m_plot;
  QLabel      *m_title;
  QLabel      *m_span;
  QHBoxLayout *m_head{nullptr};
  QTimer      *m_timer;
};

} // namespace deepvaccontrol

#endif // HOMETREND_H
