#include "delayselectdialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "utils.h"

namespace control2 {
DelaySelectDialog::DelaySelectDialog(QWidget *parent) : QDialog{parent} {
  auto *timer1 = new QTimer(this);

  auto *acceptBtn = new QPushButton(tr("Установить"), this);
  auto *cancelBtn = new QPushButton(tr("Отмена"), this);

  acceptBtn->setFixedSize(120 * utils::ssf(), 50 * utils::ssf());
  cancelBtn->setFixedSize(120 * utils::ssf(), 50 * utils::ssf());

  {
    QFont font2 = acceptBtn->font();
    font2.setPointSize(12 * utils::ssf());
    acceptBtn->setFont(font2);
    cancelBtn->setFont(font2);
  }

  auto *currentTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime());
  currentTimeEdit->setDisplayFormat("yyyy-MM-dd   HH:mm:ss");
  currentTimeEdit->setReadOnly(true);

  startTimeEdit = new QDateTimeEdit(QDateTime::currentDateTime());
  startTimeEdit->setDisplayFormat("yyyy-MM-dd   HH:mm:ss");

  auto *mainLay = new QVBoxLayout(this);

  auto *gridLay = new QGridLayout(this);
  gridLay->addWidget(new QLabel(tr("Текущее время:"), this), 0, 0);
  gridLay->addWidget(currentTimeEdit, 0, 1);
  gridLay->addWidget(new QLabel(tr("Время пуска:"), this), 1, 0);
  gridLay->addWidget(startTimeEdit, 1, 1);

  auto layBtn = new QHBoxLayout();
  layBtn->addWidget(acceptBtn);
  layBtn->addStretch();
  layBtn->addWidget(cancelBtn);

  mainLay->addLayout(gridLay);
  mainLay->addLayout(layBtn);

  connect(timer1, &QTimer::timeout, this, [this, currentTimeEdit] {
    currentTimeEdit->setDateTime(QDateTime::currentDateTime());
    checkMinToMinDateTime();
  });

  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  connect(acceptBtn, &QPushButton::clicked, this, [this] {
    emit startTimeSelected(startTimeEdit->dateTime());
    accept();
  });

  timer1->start(1000);

  checkMinToMinDateTime();

  {
    QFont font; //("Monospace")
    font.setBold(true);
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(20 * utils::ssf());
    startTimeEdit->setFont(font);
    currentTimeEdit->setFont(font);
  }
  startTimeEdit->setFixedHeight(80);
  currentTimeEdit->setFixedHeight(80);
  startTimeEdit->setStyleSheet(utils::readStyleFromFile(":/styles/dateTimeEdit.qss"));
  currentTimeEdit->setStyleSheet(utils::readStyleFromFile(":/styles/dateTimeEdit.qss"));

  setWindowTitle(tr("Отложенный пуск"));
  this->adjustSize();
  setFixedSize(size());
}

void DelaySelectDialog::checkMinToMinDateTime() {
  if (startTimeEdit->dateTime() < QDateTime::currentDateTime()) {
    startTimeEdit->setDateTime(QDateTime::currentDateTime().addSecs(60));
    startTimeEdit->setMinimumDateTime(startTimeEdit->dateTime());
  }
}
} // namespace control2
