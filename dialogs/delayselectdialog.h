#ifndef DELAYSELECTDIALOG_H
#define DELAYSELECTDIALOG_H

#include <QDateTimeEdit>
#include <QDialog>

namespace deepvaccontrol {
class DelaySelectDialog : public QDialog {
  Q_OBJECT
public:
  DelaySelectDialog(QWidget *parent = nullptr);

signals:
  void startTimeSelected(QDateTime);

private:
  void checkMinToMinDateTime();

  QDateTimeEdit *startTimeEdit;
};
} // namespace deepvaccontrol
#endif // DELAYSELECTDIALOG_H
