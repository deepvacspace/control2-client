#ifndef CONNECTIONDIALOG_H
#define CONNECTIONDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace control2 {
class ConnectionDialog : public QDialog {
  Q_OBJECT
public:
  explicit ConnectionDialog(QString const &serverId, QWidget *parent = nullptr);

  ~ConnectionDialog() = default;

  void updateContent(int contTryCount, int successCount);
  void reset();

private:
  void initGui();

  QString m_serverId;
  QLabel *m_titleLabel;
  QLabel *m_msgLabel;

  QPushButton *m_workOfflineBtn;
  QPushButton *m_cancelBtn;
  QPushButton *m_hideBtn;

protected:
  void closeEvent(QCloseEvent *);
};
} // namespace control2

#endif // CONNECTIONDIALOG_H
