#ifndef SYSLISTWIDGET_H
#define SYSLISTWIDGET_H

#include <QLabel>
#include <QWidget>

#include "resholder.h"

namespace control2 {
/**
 * @brief List of states + output indicators
 */
class SysListWidget : public QWidget {
  Q_OBJECT
public:
  explicit SysListWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  void updateList();

private:
  ResHolder       *m_resHolder;
  SystemValueList *m_states;
  QList<QLabel *>  m_values;
};
} // namespace control2

#endif // SYSLISTWIDGET_H
