#ifndef DATERANGEDIALOG_H
#define DATERANGEDIALOG_H

#include <QDateTime>
#include <QDialog>

namespace deepvaccontrol {
class DateRangeDialog : public QDialog {
  Q_OBJECT
public:
  explicit DateRangeDialog(QDateTime date1, QDateTime date2, QWidget *parent = nullptr);
signals:
  void dateSelected(QDateTime, QDateTime);
};
} // namespace deepvaccontrol

#endif // DATERANGEDIALOG_H
