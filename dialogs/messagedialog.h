#ifndef MESSAGEDIALOG_H
#define MESSAGEDIALOG_H

#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTimer>

class QLabel;

namespace deepvaccontrol {
class MessageDialog : public QDialog {
  Q_OBJECT
public:
  explicit MessageDialog(QString const &title, QString const &message, QWidget *parent = nullptr);
  ~MessageDialog() {
  }
  using QDialog::open;
  void open(int visDurationMs);
  // "warning", "error", "question", "info" (default: from the title)
  void setIconKind(QString const &kind);

protected:
  void         closeEvent(QCloseEvent *);
  QHBoxLayout *m_btnLayout;
  QPushButton *m_closeBtn;
  QTimer      *m_closeTimer{nullptr};
  QLabel      *m_iconLabel{nullptr};
};
} // namespace deepvaccontrol

#endif // MESSAGEDIALOG_H
