#ifndef SETTINGSRECOVERYDIALOG_H
#define SETTINGSRECOVERYDIALOG_H

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

namespace deepvaccontrol {

// At start, when settings.json cannot be used and no automatic copy could replace it: what is wrong, the
// backups found next to it, and the choices (exec() result: Choice).
class SettingsRecoveryDialog : public QDialog {
  Q_OBJECT
public:
  enum Choice {
    EXIT    = 0,
    RETRY   = 2, // the operator fixed or replaced the file
    RESTORE = 3  // selectedBackup()
  };

  SettingsRecoveryDialog(QString const &settingsPath, QString const &problem, QWidget *parent = nullptr);
  void    setProblem(QString const &problem);
  QString selectedBackup() const;

protected:
  void reject() override; // Escape / close = exit

private:
  void refreshBackups();

  QString      m_settingsPath;
  QLabel      *m_problem{nullptr};
  QListWidget *m_backups{nullptr};
  QPushButton *m_restoreBtn{nullptr};
};

}

#endif // SETTINGSRECOVERYDIALOG_H
