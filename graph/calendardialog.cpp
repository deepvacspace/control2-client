#include "calendardialog.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLocale>
#include <QPainter>
#include <QVBoxLayout>

#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
QString durationText(double seconds) {
  auto const total = static_cast<qint64>(std::max(0.0, seconds));
  return QStringLiteral("%1:%2:%3")
    .arg(total / 3600, 2, 10, QLatin1Char('0'))
    .arg((total / 60) % 60, 2, 10, QLatin1Char('0'))
    .arg(total % 60, 2, 10, QLatin1Char('0'));
}
}

CalendarCustomWidget::CalendarCustomWidget(QMap<QDate, int> const &counts, QWidget *parent)
    : QCalendarWidget(parent), m_counts{counts} {
  setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
  setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
  setNavigationBarVisible(false);
  setGridVisible(false);
  setLocale(QLocale(QLocale::English));
  setFirstDayOfWeek(Qt::Monday); // after the locale, which sets its own first day
}

void CalendarCustomWidget::setCounts(QMap<QDate, int> const &counts) {
  m_counts = counts;
  updateCells();
}

void CalendarCustomWidget::paintCell(QPainter *painter, QRect const &rect, QDate date) const {
  theme::Colors const &c = theme::colors();
  double const         ssf{utils::ssf()};
  painter->save();
  painter->setRenderHint(QPainter::Antialiasing);
  painter->fillRect(rect, c.panel);
  bool const selected{date == selectedDate()};
  bool const hasExperiments{m_counts.contains(date)};
  QRectF const cell = QRectF(rect).adjusted(2 * ssf, 2 * ssf, -2 * ssf, -2 * ssf);
  if (selected) {
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 50));
    painter->drawRoundedRect(cell, 4 * ssf, 4 * ssf);
  }
  if (date == QDate::currentDate()) {
    painter->setPen(QPen(c.accent, qMax(1.0, 1 * ssf)));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(cell, 4 * ssf, 4 * ssf);
  }
  QFont font = this->font();
  font.setBold(hasExperiments);
  painter->setFont(font);
  QColor text = hasExperiments ? c.text : c.muted;
  if (date.month() != monthShown()) {
    text.setAlpha(90);
  }
  painter->setPen(text);
  painter->drawText(rect.adjusted(0, 0, 0, -static_cast<int>(6 * ssf)), Qt::AlignCenter, date.toString("d"));
  if (hasExperiments) {
    // a dot under the day number
    painter->setPen(Qt::NoPen);
    painter->setBrush(date.month() == monthShown() ? c.accent : c.border);
    painter->drawEllipse(QPointF(rect.center().x(), rect.bottom() - 7 * ssf), 2.5 * ssf, 2.5 * ssf);
  }
  painter->restore();
}

CalendarDialog::CalendarDialog(QVector<storage::ExperimentSpan> const &experiments, QWidget *parent)
    : QDialog{parent}, m_experiments{experiments} {
  double const ssf{utils::ssf()};
  setWindowTitle(tr("Recorded experiments"));
  setAttribute(Qt::WA_DeleteOnClose);

  QMap<QDate, int> counts;
  for (int i{0}; i < m_experiments.size(); ++i) {
    QDate const day = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(m_experiments[i].start * 1000.0)).date();
    m_byDay[day].push_back(i);
    counts[day] += 1;
  }

  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(14 * ssf, 12 * ssf, 14 * ssf, 12 * ssf);
  mainLay->setSpacing(16 * ssf);

  // ---- left: month
  auto *left    = new QVBoxLayout();
  auto *monthBar = new QHBoxLayout();
  theme::Colors const &c = theme::colors();
  auto navButton = [&](QString const &kind, QString const &tip) {
    auto *button = new QPushButton(this);
    theme::setThemedIcon(button, kind, static_cast<int>(14 * ssf));
    button->setFixedSize(30 * ssf, 30 * ssf);
    button->setToolTip(tip);
    return button;
  };
  auto *prevBtn = navButton("left", tr("Previous month"));
  auto *nextBtn = navButton("right", tr("Next month"));
  m_monthLabel  = new QLabel(this);
  m_monthLabel->setObjectName("cardTitle");
  m_monthLabel->setAlignment(Qt::AlignCenter);
  monthBar->addWidget(prevBtn);
  monthBar->addWidget(m_monthLabel, 1);
  monthBar->addWidget(nextBtn);
  left->addLayout(monthBar);
  m_calendar = new CalendarCustomWidget(counts, this);
  m_calendar->setMinimumSize(320 * ssf, 260 * ssf);
  left->addWidget(m_calendar, 1);
  mainLay->addLayout(left);

  // ---- right: the experiments of the selected day
  auto *right = new QVBoxLayout();
  m_dayLabel  = new QLabel(this);
  m_dayLabel->setObjectName("cardTitle");
  right->addWidget(m_dayLabel);
  m_list = new QListWidget(this);
  m_list->setMinimumWidth(360 * ssf);
  m_list->setSpacing(static_cast<int>(2 * ssf));
  right->addWidget(m_list, 1);
  auto *buttons = new QHBoxLayout();
  auto *liveBtn = new QPushButton(tr("Today (live)"), this);
  liveBtn->setToolTip(tr("Back to the live data of today"));
  m_openBtn = new QPushButton(tr("Open"), this);
  m_openBtn->setObjectName("primaryButton");
  auto *cancelBtn = new QPushButton(tr("Cancel"), this);
  m_openDayBtn    = new QPushButton(tr("Open whole day"), this);
  m_openDayBtn->setToolTip(tr("All experiments of this day in one chart"));
  for (QPushButton *button : {liveBtn, m_openBtn, cancelBtn, m_openDayBtn}) {
    button->setMinimumSize(90 * ssf, 30 * ssf);
  }
  buttons->addWidget(liveBtn);
  buttons->addStretch();
  buttons->addWidget(cancelBtn);
  buttons->addWidget(m_openDayBtn);
  buttons->addWidget(m_openBtn);
  right->addLayout(buttons);
  mainLay->addLayout(right, 1);

  connect(prevBtn, &QPushButton::clicked, this, [this] { shiftMonth(-1); });
  connect(nextBtn, &QPushButton::clicked, this, [this] { shiftMonth(+1); });
  connect(m_calendar, &QCalendarWidget::selectionChanged, this, [this] { showDay(m_calendar->selectedDate()); });
  connect(m_calendar, &QCalendarWidget::currentPageChanged, this, &CalendarDialog::updateMonthLabel);
  connect(m_list, &QListWidget::itemDoubleClicked, this, &CalendarDialog::openSelected);
  connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) { m_openBtn->setEnabled(row >= 0); });
  connect(m_openBtn, &QPushButton::clicked, this, &CalendarDialog::openSelected);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_openDayBtn, &QPushButton::clicked, this, [this] {
    QDate const                      day = m_calendar->selectedDate();
    QVector<storage::ExperimentSpan> experiments;
    for (int index : m_byDay.value(day)) {
      experiments.push_back(m_experiments[index]);
    }
    if (!experiments.isEmpty()) {
      emit daySelected(day, experiments);
      accept();
    }
  });
  connect(liveBtn, &QPushButton::clicked, this, [this] {
    emit liveSelected();
    accept();
  });

  // start on today, or on the latest day with experiments
  QDate start = QDate::currentDate();
  if (!m_byDay.contains(start) && !m_byDay.isEmpty()) {
    start = m_byDay.lastKey();
  }
  m_calendar->setSelectedDate(start);
  m_calendar->setCurrentPage(start.year(), start.month());
  updateMonthLabel();
  showDay(start);
  resize(820 * ssf, 420 * ssf);
}

void CalendarDialog::showDay(QDate const &date) {
  QVector<int> const indexes = m_byDay.value(date);
  QString const day = date.toString("yyyy-MM-dd");
  m_dayLabel->setText(indexes.isEmpty()        ? tr("%1 · no experiments").arg(day)
                      : (indexes.size() == 1) ? tr("%1 · 1 experiment").arg(day)
                                              : tr("%1 · %2 experiments").arg(day).arg(indexes.size()));
  m_list->clear();
  QLocale const locale(QLocale::English);
  for (int index : indexes) {
    storage::ExperimentSpan const &experiment = m_experiments[index];
    QDateTime const                from = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(experiment.start * 1000.0));
    QDateTime const                to   = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(experiment.end * 1000.0));
    auto *item = new QListWidgetItem(QStringLiteral("%1 – %2   %3\n%4 · %5 samples")
                                       .arg(from.toString("HH:mm"), to.toString(to.date() == from.date() ? "HH:mm" : "MM-dd HH:mm"),
                                            experiment.name, durationText(experiment.end - experiment.start),
                                            locale.toString(experiment.samples)));
    item->setData(Qt::UserRole, index);
    item->setToolTip(experiment.name);
    m_list->addItem(item);
  }
  if (m_list->count() > 0) {
    m_list->setCurrentRow(m_list->count() - 1); // the latest of the day
  }
  m_openBtn->setEnabled(m_list->currentRow() >= 0);
  m_openDayBtn->setEnabled(!indexes.isEmpty());
}

void CalendarDialog::shiftMonth(int months) {
  QDate const shown = QDate(m_calendar->yearShown(), m_calendar->monthShown(), 1).addMonths(months);
  m_calendar->setCurrentPage(shown.year(), shown.month());
}

void CalendarDialog::updateMonthLabel() {
  m_monthLabel->setText(QDate(m_calendar->yearShown(), m_calendar->monthShown(), 1).toString("MMMM yyyy"));
}

void CalendarDialog::openSelected() {
  QListWidgetItem const *item = m_list->currentItem();
  if (!item) {
    return;
  }
  emit experimentSelected(m_experiments.value(item->data(Qt::UserRole).toInt()));
  accept();
}

}
