#ifndef ADVISORPROGRESSDIALOG_H
#define ADVISORPROGRESSDIALOG_H

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;

namespace deepvaccontrol::ai {

// The AI Advisor's "calculating PID coefficients" window: a bar and its percentage. Cancel, Escape or closing the
// window asks for the calculation to stop (cancelRequested); the window stays until finish().
class AdvisorProgressDialog : public QDialog {
  Q_OBJECT
public:
  explicit AdvisorProgressDialog(QWidget *parent = nullptr);

  void setProgress(int done, int total);
  // the calculation is over (finished or cancelled): the window closes
  void finish();

signals:
  void cancelRequested();

protected:
  void reject() override;
  void closeEvent(QCloseEvent *event) override;

private:
  QProgressBar *m_bar;
  QLabel       *m_percent;
  QLabel       *m_title;
  QPushButton  *m_cancelBtn;
  bool          m_finished{false};
  bool          m_cancelling{false};
};

}

#endif // ADVISORPROGRESSDIALOG_H
