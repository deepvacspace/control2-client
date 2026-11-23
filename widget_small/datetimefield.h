#ifndef DATETIMEFIELD_H
#define DATETIMEFIELD_H

#include <QDateTime>
#include <QWidget>

class QDateEdit;

namespace deepvaccontrol {
class LineEditTime;

/**
 * @brief Date and time for touch panels: the date from a calendar, the time (hh:mm:ss) from the on-screen number pad.
 */
class DateTimeField : public QWidget {
  Q_OBJECT
public:
  explicit DateTimeField(QDateTime const &value = {}, QWidget *parent = nullptr);

  [[nodiscard]] QDateTime dateTime() const;
  void                    setDateTime(QDateTime const &value);

signals:
  void dateTimeChanged();

private:
  QDateEdit    *m_date;
  LineEditTime *m_time;
};

}

#endif // DATETIMEFIELD_H
