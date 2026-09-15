#include "calendardialog.h"

#include <QDir>
#include <QGuiApplication>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "qscreen.h"
#include "utils.h"

namespace deepvaccontrol {
CalendarCustomWidget::CalendarCustomWidget(QList<QDate> const &dateList, QWidget *parent)
    : QCalendarWidget(parent), m_datelist{dateList} {
  setStyleSheet(utils::readStyleFromFile(":/styles/calendarWidget.qss"));
}

void CalendarCustomWidget::paintCell(QPainter *painter, QRect const &rect, QDate date) const {
  QFont font;
  if (m_datelist.contains(date)) {
    font.setPixelSize(18 * utils::ssf());
    font.setBold(true);
  } else {
    font.setPixelSize(12 * utils::ssf());
  }
  if (date.month() != monthShown()) {
    painter->setPen(QColor(150, 150, 150));
  } else {
    painter->setPen(Qt::black);
  }
  painter->setFont(font);
  painter->drawText(rect, Qt::AlignCenter, date.toString("d"));
}

CalendarDialog::CalendarDialog(QString const &dirPath, QWidget *parent)
    : QDialog{parent}
    , m_monthNameList{tr("Январь"),
                      tr("Февраль"),
                      tr("Март"),
                      tr("Апрель"),
                      tr("Май"),
                      tr("Июнь"),
                      tr("Июль"),
                      tr("Август"),
                      tr("Сентябрь"),
                      tr("Октябрь"),
                      tr("Ноябрь"),
                      tr("Декабрь")}
    , m_dirPath{dirPath} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf(), 2 * utils::ssf());
  mainLay->setSpacing(2 * utils::ssf());

  scanFolder();
  m_calendarTable = new CalendarCustomWidget(m_dateList, this);
  mainLay->addWidget(m_calendarTable);

  auto *prevM         = new QPushButton(this);
  auto *nextM         = new QPushButton(this);
  auto *today         = new QPushButton(tr("Cегодня"), this);
  m_currentMonthLabel = new QLabel(m_monthNameList[QDate::currentDate().month() - 1]);

  QFont font;
  font.setBold(true);
  font.setPointSize(16 * utils::ssf());
  m_currentMonthLabel->setFont(font);
  m_currentMonthLabel->setAlignment(Qt::AlignCenter);

  prevM->setIcon(QIcon(":/imgs/listArrowLeft.png"));
  nextM->setIcon(QIcon(":/imgs/listArrowRight.png"));

  for (auto *btn : {prevM, nextM, today}) {
    btn->setFixedSize(CALENDAR_BTN_SIZE * utils::ssf(), CALENDAR_BTN_SIZE * utils::ssf());
    btn->setIconSize(QSize(CALENDAR_BTN_IMG_SIZE * utils::ssf(), CALENDAR_BTN_IMG_SIZE * utils::ssf()));
  }
  today->setFixedSize(1.2 * CALENDAR_BTN_SIZE * utils::ssf(), CALENDAR_BTN_SIZE * utils::ssf());
  m_currentMonthLabel->setFixedWidth(100 * utils::ssf());

  auto *btnsLay = new QHBoxLayout();
  btnsLay->addWidget(prevM);
  btnsLay->addWidget(m_currentMonthLabel);
  btnsLay->addWidget(nextM);
  btnsLay->addWidget(today);
  btnsLay->addStretch();

  mainLay->addLayout(btnsLay);

  this->setWindowFlags(Qt::Popup);

  m_calendarTable->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
  m_calendarTable->setGridVisible(true);
  m_calendarTable->setNavigationBarVisible(false);

  // center
  double const x{QGuiApplication::screens().first()->geometry().width() / 2 - 400 * utils::ssf() / 2};
  double const y{QGuiApplication::screens().first()->geometry().height() / 2 - 400 * utils::ssf() / 2};
  setGeometry(x, y, 400 * utils::ssf(), 400 * utils::ssf());

  connect(m_calendarTable, &CalendarCustomWidget::clicked, this, &CalendarDialog::checkSelectedDate);

  connect(prevM, &QPushButton::clicked, this, [this] {
    shiftMonth(-1);
  });
  connect(nextM, &QPushButton::clicked, this, [this] {
    shiftMonth(+1);
  });
  connect(today, &QPushButton::clicked, this, [this] {
    checkSelectedDate(QDate::currentDate());
  });
}

void CalendarDialog::scanFolder() {
  m_dateList.clear();

  QDir const        directory(m_dirPath);
  QStringList const files{directory.entryList({"*.dat"}, QDir::Files)};
  for (auto const &filename : files) {
    QDate const date{QDate::fromString(filename.left(10), "yyyy-MM-dd")};
    if (!m_dateList.contains(date)) {
      m_dateList.push_back(date);
    }
  }

  if (!m_dateList.contains(QDate::currentDate())) {
    m_dateList.push_back(QDate::currentDate());
  }
}

void CalendarDialog::checkSelectedDate(QDate const &date) {
  bool load{m_dateList.contains(date)};

  if (load) {
    QDateTime dateTime(date, QTime{});
    emit      intervalKeySelected(static_cast<double>(dateTime.toSecsSinceEpoch()));
    close();
  }
}

void CalendarDialog::shiftMonth(int sh) {
  int m{m_calendarTable->monthShown()};
  int y{m_calendarTable->yearShown()};
  m += sh;
  if (m > 12) {
    m = 1;
    y++;
  }
  if (m < 1) {
    m = 12;
    y--;
  }
  m_calendarTable->setCurrentPage(y, m);
  m_currentMonthLabel->setText(m_monthNameList[m - 1]);
}

} // namespace deepvaccontrol
