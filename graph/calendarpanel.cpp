#include "calendarpanel.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "calendardialog.h"
#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

QPushButton *link(QString const &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setProperty("link", true);
  button->setFocusPolicy(Qt::NoFocus);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

QDateTime toDateTime(double epoch) {
  return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(epoch * 1000.0));
}
} // namespace

CalendarPanel::CalendarPanel(QWidget *parent) : QWidget{parent} {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, px(8), 0);
  lay->setSpacing(px(8));

  // the month, with its arrows
  auto *monthBar = new QHBoxLayout();
  auto  arrow    = [this](QString const &kind, QString const &tip) {
    auto *button = new QPushButton(this);
    theme::setThemedIcon(button, kind, px(13));
    button->setFixedSize(px(30), px(30));
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
  };
  auto *prevBtn = arrow(QStringLiteral("left"), tr("Previous month"));
  auto *nextBtn = arrow(QStringLiteral("right"), tr("Next month"));
  m_monthLabel  = new QLabel(this);
  m_monthLabel->setObjectName("cardTitle");
  m_monthLabel->setAlignment(Qt::AlignCenter);
  monthBar->addWidget(prevBtn);
  monthBar->addWidget(m_monthLabel, 1);
  monthBar->addWidget(nextBtn);
  lay->addLayout(monthBar);
  m_calendar = new CalendarCustomWidget({}, this);
  m_calendar->setMinimumHeight(px(230));
  lay->addWidget(m_calendar);

  auto *line = new QFrame(this);
  line->setObjectName("cardDivider");
  line->setFixedHeight(1);
  lay->addWidget(line);

  // the day: its experiments, one tap opens one
  auto *dayBar = new QHBoxLayout();
  m_dayLabel   = new QLabel(this);
  m_dayLabel->setObjectName("cardTitle");
  m_dayBtn = link(tr("Day overview"), this);
  m_dayBtn->setToolTip(tr("All experiments of this day in one chart"));
  dayBar->addWidget(m_dayLabel, 1);
  dayBar->addWidget(m_dayBtn);
  lay->addLayout(dayBar);
  m_list = new QListWidget(this);
  m_list->setObjectName("statsList");
  m_list->setMinimumHeight(px(200));
  touch::enableScrolling(m_list);
  lay->addWidget(m_list, 1);
  auto *liveBtn = link(tr("Back to the live data of today"), this);
  lay->addWidget(liveBtn);

  connect(prevBtn, &QPushButton::clicked, this, [this] {
    QDate const shown = QDate(m_calendar->yearShown(), m_calendar->monthShown(), 1).addMonths(-1);
    m_calendar->setCurrentPage(shown.year(), shown.month());
  });
  connect(nextBtn, &QPushButton::clicked, this, [this] {
    QDate const shown = QDate(m_calendar->yearShown(), m_calendar->monthShown(), 1).addMonths(1);
    m_calendar->setCurrentPage(shown.year(), shown.month());
  });
  connect(m_calendar, &QCalendarWidget::selectionChanged, this, [this] { showDay(m_calendar->selectedDate()); });
  connect(m_calendar, &QCalendarWidget::currentPageChanged, this, &CalendarPanel::updateMonthLabel);
  connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
    emit experimentSelected(m_experiments.value(item->data(Qt::UserRole).toInt()));
  });
  connect(m_dayBtn, &QPushButton::clicked, this, [this] {
    QDate const                      day = m_calendar->selectedDate();
    QVector<storage::ExperimentSpan> experiments;
    for (int const index : m_byDay.value(day)) {
      experiments.push_back(m_experiments[index]);
    }
    if (!experiments.isEmpty()) {
      emit daySelected(day, experiments);
    }
  });
  connect(liveBtn, &QPushButton::clicked, this, &CalendarPanel::liveSelected);
  updateMonthLabel();
}

void CalendarPanel::setExperiments(QVector<storage::ExperimentSpan> const &experiments, QHash<QString, int> const &numbers) {
  m_experiments = experiments;
  m_numbers     = numbers;
  m_byDay.clear();
  QMap<QDate, int> counts;
  for (int i{0}; i < m_experiments.size(); ++i) {
    QDate const day = toDateTime(m_experiments[i].start).date();
    m_byDay[day].push_back(i);
    counts[day] += 1;
  }
  m_calendar->setCounts(counts);
  // today, or the latest day with experiments, the first time
  QDate day = m_calendar->selectedDate();
  if (!m_byDay.contains(day)) {
    day = m_byDay.contains(QDate::currentDate()) || m_byDay.isEmpty() ? QDate::currentDate() : m_byDay.lastKey();
  }
  m_calendar->setSelectedDate(day);
  m_calendar->setCurrentPage(day.year(), day.month());
  updateMonthLabel();
  showDay(day);
}

void CalendarPanel::showDay(QDate const &date) {
  QVector<int> const indexes = m_byDay.value(date);
  QString const      day     = QLocale(QLocale::English).toString(date, QStringLiteral("d MMM"));
  m_dayLabel->setText(indexes.isEmpty()        ? tr("%1 · no experiments").arg(day)
                      : (indexes.size() == 1) ? tr("%1 · 1 experiment").arg(day)
                                              : tr("%1 · %2 experiments").arg(day).arg(indexes.size()));
  m_dayBtn->setEnabled(!indexes.isEmpty());
  m_list->clear();
  // number, start time, duration
  for (int const index : indexes) {
    storage::ExperimentSpan const &experiment = m_experiments[index];
    auto const                     total      = static_cast<qint64>(std::max(0.0, experiment.end - experiment.start));
    auto *row    = new QWidget();
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(px(10), 0, px(10), 0);
    rowLay->setSpacing(px(12));
    int const number{m_numbers.value(experiment.key)};
    auto     *numberLabel = new QLabel(number > 0 ? QStringLiteral("#%1").arg(number) : QStringLiteral("—"), row);
    numberLabel->setProperty("muted", true);
    numberLabel->setMinimumWidth(px(46));
    auto *timeLabel = new QLabel(toDateTime(experiment.start).toString(QStringLiteral("HH:mm")), row);
    timeLabel->setObjectName("statsValue");
    QFont bold = timeLabel->font();
    bold.setBold(true);
    timeLabel->setFont(bold);
    auto *durationLabel = new QLabel(QStringLiteral("%1:%2").arg(total / 3600, 2, 10, QLatin1Char('0')).arg((total / 60) % 60, 2, 10, QLatin1Char('0')), row);
    durationLabel->setProperty("muted", true);
    for (QLabel *label : {numberLabel, timeLabel, durationLabel}) {
      label->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    rowLay->addWidget(numberLabel);
    rowLay->addWidget(timeLabel);
    rowLay->addStretch();
    rowLay->addWidget(durationLabel);
    auto *item = new QListWidgetItem(m_list);
    item->setData(Qt::UserRole, index);
    item->setToolTip(experiment.name);
    item->setSizeHint(QSize(0, px(38)));
    m_list->setItemWidget(item, row);
  }
}

void CalendarPanel::updateMonthLabel() {
  m_monthLabel->setText(QLocale(QLocale::English).toString(QDate(m_calendar->yearShown(), m_calendar->monthShown(), 1), QStringLiteral("MMMM yyyy")));
}

} // namespace deepvaccontrol
