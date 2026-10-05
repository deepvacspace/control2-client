#ifndef SYSLISTWIDGET_H
#define SYSLISTWIDGET_H

#include <QLabel>
#include <QWidget>

#include "resholder.h"

class QTableWidget;

namespace deepvaccontrol {
/**
 * @brief System -> States: every value received from the PLC (name, current value, unit) and the state of
 * the outputs configured in "monitor"
 */
class SysListWidget : public QWidget {
  Q_OBJECT
public:
  explicit SysListWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  void updateList();

private:
  struct Output {
    SystemValue *value{nullptr};
    int          bit{0};
    QLabel      *badge{nullptr};
  };

  ResHolder       *m_resHolder;
  SystemValueList *m_states;
  QTableWidget    *m_table{nullptr};
  QVector<Output>  m_outputs;
};
} // namespace deepvaccontrol

#endif // SYSLISTWIDGET_H
