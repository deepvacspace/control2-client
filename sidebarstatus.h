#ifndef SIDEBARSTATUS_H
#define SIDEBARSTATUS_H

#include <QVector>
#include <QWidget>

#include "resholder.h"

class QLabel;

namespace deepvaccontrol {

/**
 * @brief Navigation sidebar, at the bottom over the clock: the connection and chamber status (and the job's kind), then the
 * temperature and pressure (and humidity, on a chamber with it) with their setpoints. Replaces the former top bar.
 */
class SidebarStatus : public QWidget {
  Q_OBJECT
public:
  explicit SidebarStatus(ResHolder *resHolder, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void updateStatus();
  void updateValues();
  void restyle();
  void fitStatus(); // the status in letters small enough for its box

  // a value of statusBarStatesList(): its unit and setpoint ("°C → 24.0") over the value
  struct Value {
    QString  name;
    QWidget *box{nullptr};
    QLabel  *value{nullptr};
    QLabel  *setpoint{nullptr};
  };

  ResHolder      *m_resHolder;
  QLabel         *m_status{nullptr};
  QLabel         *m_job{nullptr};
  QVector<Value>  m_values;
};

/**
 * @brief Navigation sidebar, at the bottom: the clock and the logo (the wordmark, reverse: white on the dark sidebar).
 */
class SidebarFooter : public QWidget {
  Q_OBJECT
public:
  explicit SidebarFooter(QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *event) override;

private:
  void restyle();

  QLabel *m_clock{nullptr};
  QLabel *m_brand{nullptr};
};

}

#endif // SIDEBARSTATUS_H
