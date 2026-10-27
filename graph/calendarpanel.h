#ifndef CALENDARPANEL_H
#define CALENDARPANEL_H

#include <QDate>
#include <QHash>
#include <QMap>
#include <QWidget>

#include "runstore.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace deepvaccontrol {
class CalendarCustomWidget;

/**
 * @brief Chart page side panel: the recorded experiments by day. A month (days with experiments marked), the
 * experiments of the day chosen (one tap opens one in the active pane), the whole day, or back to the live data.
 */
class CalendarPanel : public QWidget {
  Q_OBJECT
public:
  explicit CalendarPanel(QWidget *parent = nullptr);

  // numbers: "#N" of an experiment (by its key), as in the experiment list
  void setExperiments(QVector<storage::ExperimentSpan> const &experiments, QHash<QString, int> const &numbers);

signals:
  void experimentSelected(storage::ExperimentSpan const &experiment);
  void daySelected(QDate const &day, QVector<storage::ExperimentSpan> const &experiments);
  void liveSelected();

private:
  void showDay(QDate const &date);
  void updateMonthLabel();

  QVector<storage::ExperimentSpan> m_experiments;
  QHash<QString, int>              m_numbers;
  QMap<QDate, QVector<int>>        m_byDay; // indexes into m_experiments
  CalendarCustomWidget            *m_calendar{nullptr};
  QLabel                          *m_monthLabel{nullptr};
  QLabel                          *m_dayLabel{nullptr};
  QPushButton                     *m_dayBtn{nullptr};
  QListWidget                     *m_list{nullptr};
};

}

#endif // CALENDARPANEL_H
