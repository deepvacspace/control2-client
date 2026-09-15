#ifndef ACTIONWIDGET_H
#define ACTIONWIDGET_H

#include <QListWidget>
#include <QWidget>

#include "actionmanager.h"

namespace deepvaccontrol {
class ActionWidget : public QWidget {
  Q_OBJECT
public:
  explicit ActionWidget(ActionManager *actionManager, QWidget *parent = nullptr);

private:
  void addElements(QVariantList const &newElements, bool addToBottom = false);
  ActionManager *m_actionManager;
  QListWidget   *m_listWidget;
};
} // namespace deepvaccontrol

#endif // ACTIONWIDGET_H
