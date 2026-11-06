#ifndef SIDEBARSTATUS_H
#define SIDEBARSTATUS_H

#include <QVector>
#include <QWidget>

#include "resholder.h"

class QLabel;

namespace deepvaccontrol {

/**
 * @brief Navigation sidebar, under Home: the connection and chamber status (and the job's kind), then the
 * temperature and pressure (and humidity, on a chamber with it) with their setpoints. Replaces the former top bar.
 */
class SidebarStatus : public QWidget {
  Q_OBJECT
public:
  explicit SidebarStatus(ResHolder *resHolder, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *event) override;

private:
  void updateStatus();
  void updateValues();
  void restyle();

  // a value of statusBarStatesList(): the value, its unit and its setpoint, one over the other
  struct Value {
    QString  name;
    QWidget *box{nullptr};
    QLabel  *value{nullptr};
    QLabel  *unit{nullptr};
    QLabel  *setpoint{nullptr};
  };

  ResHolder      *m_resHolder;
  QLabel         *m_status{nullptr};
  QLabel         *m_job{nullptr};
  QVector<Value>  m_values;
};

/**
 * @brief Navigation sidebar, at the bottom: the clock and the product name (a logo is to come).
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
