#ifndef USERWIDGET_H
#define USERWIDGET_H

#include <QDateTime>
#include <QLabel>
#include <QTableWidget>
#include <QPushButton>
#include <QWidget>

#include "accessmanager.h"
#include "logger.h"

namespace deepvaccontrol {
class UserWidget : public QWidget {
  Q_OBJECT
public:
  explicit UserWidget(Logger *logger, AccessManager *accessManager, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *event) override;

private:
  void    initGui();
  void    updateBtnsEnable();
  void    updateUsersList();
  QString getSelectedUser() const;
  void    switchUser();

  Logger        *m_logger;
  AccessManager *m_accessManager;

  QTableWidget *m_usersList;
  QLabel       *m_currentName{nullptr};
  QLabel       *m_currentLevel{nullptr};
  QLabel       *m_loggedInSince{nullptr};
  QDateTime     m_since;
  void          updateCurrentUser(QString const &name, AccessManager::AccessLevel level);

  QPushButton *m_selectUserBtn;
  QPushButton *m_editUserBtn;
  QPushButton *m_addUserBtn;
  QPushButton *m_delUserBtn;
};
} // namespace deepvaccontrol

#endif // USERWIDGET_H
