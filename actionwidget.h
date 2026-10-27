#ifndef ACTIONWIDGET_H
#define ACTIONWIDGET_H

#include <QTableWidget>
#include <QWidget>

#include "actionmanager.h"

namespace deepvaccontrol {
// System -> Log: the event log as a table (time, source, message), newest first.
class ActionWidget : public QWidget {
  Q_OBJECT
public:
  explicit ActionWidget(ActionManager *actionManager, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *event) override;

private:
  void          addElements(QVariantList const &newElements, bool addToBottom = false);
  static QColor sourceColor(quint8 type);

  ActionManager *m_actionManager;
  QTableWidget  *m_table;
};
}

#endif // ACTIONWIDGET_H
