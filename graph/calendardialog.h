#ifndef CALENDARDIALOG_H
#define CALENDARDIALOG_H

#include <QCalendarWidget>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPushButton>

#include "runstore.h"

namespace deepvaccontrol {

// Month view; days with recorded experiments are bold and marked.
class CalendarCustomWidget : public QCalendarWidget {
  Q_OBJECT
public:
  explicit CalendarCustomWidget(QMap<QDate, int> const &counts, QWidget *parent = nullptr);

protected:
  void paintCell(QPainter *painter, QRect const &rect, QDate date) const override;

private:
  QMap<QDate, int> m_counts; // experiments per day
};

// Chart page: the experiments of the experiment database by day; one is opened in the chart,
// or the live data of today.
class CalendarDialog : public QDialog {
  Q_OBJECT
public:
  explicit CalendarDialog(QVector<storage::ExperimentSpan> const &experiments, QWidget *parent = nullptr);

signals:
  void experimentSelected(storage::ExperimentSpan const &experiment);
  void liveSelected();

private:
  void showDay(QDate const &date);
  void shiftMonth(int months);
  void updateMonthLabel();
  void openSelected();

  QVector<storage::ExperimentSpan> m_experiments;
  QMap<QDate, QVector<int>>        m_byDay; // indexes into m_experiments
  CalendarCustomWidget            *m_calendar{nullptr};
  QLabel                          *m_monthLabel{nullptr};
  QLabel                          *m_dayLabel{nullptr};
  QListWidget                     *m_list{nullptr};
  QPushButton                     *m_openBtn{nullptr};
};

} // namespace deepvaccontrol

#endif // CALENDARDIALOG_H
