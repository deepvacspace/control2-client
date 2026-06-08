#ifndef CALENDARDIALOG_H
#define CALENDARDIALOG_H

#include <QCalendarWidget>
#include <QDialog>
#include <QLabel>
#include <QList>

namespace control2 {

constexpr inline int const CALENDAR_BTN_IMG_SIZE{50};
constexpr inline int const CALENDAR_BTN_SIZE{60};

class CalendarCustomWidget : public QCalendarWidget {
  Q_OBJECT
public:
  CalendarCustomWidget(QList<QDate> const &dateList, QWidget *parent = nullptr);

private:
  // void         paintCell(QPainter *painter, QRect const &rect, QDate const &date) const;
  QList<QDate> m_datelist;

  // QCalendarWidget interface
protected:
  void paintCell(QPainter *painter, QRect const &rect, QDate date) const override;
};

class CalendarDialog : public QDialog {
  Q_OBJECT
public:
  explicit CalendarDialog(QString const &dirPath, QWidget *parent = nullptr);

signals:
  void intervalKeySelected(double);

private:
  void                  scanFolder();
  QList<QDate>          m_dateList;
  void                  checkSelectedDate(QDate const &date);
  void                  shiftMonth(int sh);
  CalendarCustomWidget *m_calendarTable;
  QLabel               *m_currentMonthLabel;
  int                   m_currentMonthIndex;
  QStringList           m_monthNameList;
  QString               m_dirPath;
};

} // namespace control2

#endif // CALENDARDIALOG_H
