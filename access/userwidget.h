#ifndef USERWIDGET_H
#define USERWIDGET_H

#include <QListWidget>
#include <QPushButton>
#include <QWidget>

#include "accessmanager.h"
#include "logger.h"

namespace control2 {
class UserWidget : public QWidget {
  Q_OBJECT
public:
  explicit UserWidget(Logger *logger, AccessManager *accessManager, QWidget *parent = nullptr);

private:
  void    initGui();
  void    updateBtnsEnable();
  void    updateUsersList();
  QString getSelectedUser() const;
  void    switchUser();

  Logger        *m_logger;
  AccessManager *m_accessManager;

  QListWidget *m_usersList;

  QPushButton *m_selectUserBtn;
  QPushButton *m_editUserBtn;
  QPushButton *m_addUserBtn;
  QPushButton *m_delUserBtn;
};
} // namespace control2

#endif // USERWIDGET_H
