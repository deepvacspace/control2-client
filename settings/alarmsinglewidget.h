#ifndef ALARMSINGLEWIDGET_H
#define ALARMSINGLEWIDGET_H

#include <QLabel>
#include <QRadioButton>
#include <QWidget>

#include "systemvalue.h"

namespace deepvaccontrol {
class AlarmSingleWidget : public QWidget {
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

  QFrame *curState;
  QFrame *box;

  QRadioButton *onLock;
  QRadioButton *onUnlock;
  QRadioButton *off;
};

inline bool AlarmSingleWidget::getPosition() const {
  return onLock ? onLock->isChecked() : false;
}

inline bool AlarmSingleWidget::getActive() const {
  return off ? !off->isChecked() : false;
}
} // namespace deepvaccontrol

#endif // ALARMSINGLEWIDGET_H
