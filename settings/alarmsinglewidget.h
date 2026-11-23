#ifndef ALARMSINGLEWIDGET_H
#define ALARMSINGLEWIDGET_H

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "systemvalue.h"

namespace deepvaccontrol {
// One alarm input on Settings -> Alarms: its state and when it triggers (contact closed, open, or off).
class AlarmSingleWidget : public QFrame {
  Q_OBJECT
public:
  explicit AlarmSingleWidget(QString const &label,
                             SystemValue   *statusValue,
                             SystemValue   *enableValue,
                             SystemValue   *conditionValue,
                             quint32        index,
                             // bool
                             QWidget       *parent = nullptr);

  void setActiveAndPosition(bool active, bool position);
  bool getPosition() const;
  bool getActive() const;

protected:
  void changeEvent(QEvent *event) override;

private:
  void initGui();
  void updateStatusColors();
  void updateSelection();
  void updateConfigValue();

  QString m_label;

  SystemValue *m_statusValue; // переменная с массивом входных сигналов
  SystemValue *m_enableValue; // переменная с массивом активности каналов аварий
  SystemValue *m_conditionValue; // переменная с массивом критерий срабатывания каналов аварий
  quint32 m_index; // номер канала аварии
  int  m_triggered{-1}; // row colour shown: -1 none yet, 0 normal, 1 alarm
  bool m_restyling{false}; // restyling sends a palette change: no recursion

  QLabel      *curState; // input state badge
  QPushButton *onLock;   // triggers when the contact is closed
  QPushButton *onUnlock; // ... open
  QPushButton *off;
};

inline bool AlarmSingleWidget::getPosition() const {
  return onLock ? onLock->isChecked() : false;
}

inline bool AlarmSingleWidget::getActive() const {
  return off ? !off->isChecked() : false;
}
}

#endif // ALARMSINGLEWIDGET_H
