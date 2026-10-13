#include "datetimefield.h"

#include <QDateEdit>
#include <QFontMetrics>
#include <QHBoxLayout>

#include "lineedittime.h"
#include "utils.h"

namespace deepvaccontrol {

DateTimeField::DateTimeField(QDateTime const &value, QWidget *parent)
    : QWidget{parent}, m_date{new QDateEdit(this)}, m_time{new LineEditTime(0, 0, 86399, true, this)} {
  auto *lay = new QHBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(6 * utils::ssf());
  m_date->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  m_date->setCalendarPopup(true); // a calendar for fingers
  m_date->setMinimumHeight(34 * utils::ssf());
  m_time->setMinimumHeight(34 * utils::ssf());
  m_time->setToolTip(tr("Time of day: tap to type it"));
  // one look for both parts: the app's text size, digits of equal width
  QFont digits = font();
  digits.setFamily(QStringLiteral("Consolas"));
  digits.setStyleHint(QFont::TypeWriter);
  digits.setBold(false);
  m_time->setFont(digits);
  m_date->setFont(digits);
  m_time->setFixedWidth(QFontMetrics(digits).horizontalAdvance(QStringLiteral("000:00:00")) + static_cast<int>(22 * utils::ssf()));
  m_time->setStyleSheet(QString{});
  for (QWidget *part : {static_cast<QWidget *>(m_date), static_cast<QWidget *>(m_time)}) {
    part->setProperty("soft", true);
  }
  lay->addWidget(m_date, 1);
  lay->addWidget(m_time);
  setDateTime(value.isValid() ? value : QDateTime::currentDateTime());
  connect(m_date, &QDateEdit::dateChanged, this, &DateTimeField::dateTimeChanged);
  connect(m_time, &LineEditTime::userEdited, this, &DateTimeField::dateTimeChanged);
}

QDateTime DateTimeField::dateTime() const {
  return QDateTime(m_date->date(), QTime(0, 0)).addSecs(m_time->getSeconds());
}

void DateTimeField::setDateTime(QDateTime const &value) {
  m_date->setDate(value.date());
  m_time->setTime(static_cast<quint32>(QTime(0, 0).secsTo(value.time())));
}

} // namespace deepvaccontrol
