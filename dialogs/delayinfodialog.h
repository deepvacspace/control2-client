#ifndef DELAYINFODIALOG_H
#define DELAYINFODIALOG_H

#include <QDateTime>
#include <QDialog>
#include <QLabel>
#include <QTimer>

namespace deepvaccontrol {
class DelayInfoDialog : public QDialog {
  Q_OBJECT
public:
  explicit DelayInfoDialog(QString const &desription, QDateTime startTime, QWidget *parent = nullptr);

private:
  void      checkTime();
  QTimer   *m_timer;
  QDateTime m_startTime;

  QLabel *m_timeLabelCurrent;
};
} // namespace deepvaccontrol

#endif // DELAYINFODIALOG_H
