#ifndef ALARMDIALOG_H
#define ALARMDIALOG_H

#include <QDialog>
#include <QLabel>

namespace deepvaccontrol {
class AlarmDialog : public QDialog {
  Q_OBJECT
public:
  explicit AlarmDialog(QWidget *parent = nullptr);
  void showAlarmsList(QStringList alarmLabelsList);
  ~AlarmDialog() override {
    qDebug() << "~AlarmDialog()";
  }

private:
  void onClose();

  QLabel *m_alarmsListLabel;

  // QWidget interface
protected:
  void closeEvent(QCloseEvent *event) override;
};
} // namespace deepvaccontrol

#endif // ALARMDIALOG_H
