#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace deepvaccontrol {
class ConnectionDialog : public QDialog {
  Q_OBJECT
public:
  explicit ConnectionDialog(QString const &serverId, QWidget *parent = nullptr);

  ~ConnectionDialog() = default;

  void updateContent(int contTryCount, int successCount);
  void reset();
  // the chamber the messages name (Settings -> TCP may switch it)
  void setServerId(QString const &serverId);

private:
  void initGui();

  QString m_serverId;
  QLabel *m_titleLabel;
  QLabel *m_msgLabel;
  QLabel *m_iconLabel{nullptr};
  QLabel *m_attemptLabel{nullptr};
  QLabel *m_hintLabel{nullptr};

  QPushButton *m_workOfflineBtn;
  QPushButton *m_cancelBtn;
  QPushButton *m_hideBtn;

protected:
  void closeEvent(QCloseEvent *);
};
} // namespace deepvaccontrol

#endif // CONNECTIONDIALOG_H
