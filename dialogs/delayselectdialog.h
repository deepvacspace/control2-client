#ifndef DELAYSELECTDIALOG_H
#define DELAYSELECTDIALOG_H

#include <QDateTimeEdit>
#include <QDialog>

namespace control2 {
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
} // namespace control2
#endif // DELAYSELECTDIALOG_H
