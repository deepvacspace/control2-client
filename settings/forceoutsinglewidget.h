#ifndef FORCEOUTSINGLEWIDGET_H
#define FORCEOUTSINGLEWIDGET_H

#include <QButtonGroup>
#include <QFrame>
#include <QLabel>
#include <QPushButton>

#include "lineeditnum.h"
#include "systemvalue.h"

namespace deepvaccontrol {
// One output on System -> Manual output control: its state, and how it is forced
// (Auto = released to the PLC, Off, On, Duty cycle); "Set" sends the choice.
class ForceOutSingleWidget : public QFrame {
  Q_OBJECT
public:
  enum class ForceType {
    ON_OFF = 1,
    DUTY   = 2
  };
  explicit ForceOutSingleWidget(int            index,
                                QString const &stateLabel,
                                SystemValue   *sysValue,
                                ForceType      type   = ForceType::ON_OFF,
                                QWidget       *parent = nullptr);
  void       resetSelection();
  void       resetSetBtn();
  QByteArray getRawData() const;

signals:
  void generateRaw(int /*always 1 here*/ count, QByteArray data);

protected:
  void changeEvent(QEvent *event) override;

private:
  void selectMode(int mode);
  void updateStateBadge();

  int m_index;
  bool m_restyling{false}; // restyling sends a palette change: no recursion

  SystemValue  *m_sysValue{nullptr};
  QLabel       *m_stateBadge{nullptr};
  QButtonGroup *m_modeGroup{nullptr};
  QPushButton  *m_setBtn{nullptr};
  QWidget      *m_dutyBox{nullptr};

  LineEditNum *duty{nullptr};
  LineEditNum *period{nullptr};

  uint plcType = 0; // 0 auto (released), 1 off, 2 on, 3 duty cycle
};
} // namespace deepvaccontrol

#endif // FORCEOUTSINGLEWIDGET_H
