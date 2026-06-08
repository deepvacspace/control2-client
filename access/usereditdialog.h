#ifndef USEREDITDIALOG_H
#define USEREDITDIALOG_H

#include <QButtonGroup>
#include <QDialog>
#include <QPushButton>
#include <QRadioButton>

#include "accessmanager.h"
#include "lineeditscreen.h"

namespace control2 {

class AccessLevelWidget : public QWidget {
  Q_OBJECT
public:
  AccessLevelWidget(QWidget *parent = nullptr);
  void setMaxLevel(int maxLevel);
  void setLevel(int level);
  int  getLevel() const;

private:
  QVector<QString>        m_labels;
  QVector<QRadioButton *> m_radios;
  QButtonGroup           *m_btns;
};

class UserEditDialog : public QDialog {
  Q_OBJECT
public:
  explicit UserEditDialog(AccessManager *accessManager, QString const &editName, QWidget *parent = nullptr);

private:
  void    initGui();
  void    saveHandle();
  QString generateNewName();

  AccessManager     *m_accessManager;
  bool               m_addNew;
  LineEditScreen    *m_nameLine;
  LineEditScreen    *m_passLine;
  AccessLevelWidget *m_levelEdit;
  QPushButton       *m_saveBtn;
};
} // namespace control2

#endif // USEREDITDIALOG_H
