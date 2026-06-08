#ifndef MESSAGEDIALOG_H
#define MESSAGEDIALOG_H

#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>

namespace control2 {
class MessageDialog : public QDialog {
  Q_OBJECT
public:
  explicit MessageDialog(QString const &title, QString const &message, QWidget *parent = nullptr);
  ~MessageDialog() {
  }
  using QDialog::open;
  void open(int visDurationMs);

protected:
  void         closeEvent(QCloseEvent *);
  QHBoxLayout *m_btnLayout;
  QPushButton *m_closeBtn;
  QTimer      *m_closeTimer;
};
} // namespace control2

#endif // MESSAGEDIALOG_H
