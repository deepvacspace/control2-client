#include "daterangedialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimeEdit>

#include "utils.h"

namespace deepvaccontrol {
DateRangeDialog::DateRangeDialog(QDateTime date1, QDateTime date2, QWidget *parent) : QDialog{parent} {
  auto *acceptBtn = new QPushButton(tr("Установить"), this);
  auto *cancelBtn = new QPushButton(tr("Отмена"), this);

  acceptBtn->setFixedSize(120 * utils::ssf(), 50 * utils::ssf());
  cancelBtn->setFixedSize(120 * utils::ssf(), 50 * utils::ssf());

  auto *startTimeEdit = new QDateTimeEdit(date1);
  auto *endTimeEdit   = new QDateTimeEdit(date2);

  startTimeEdit->setDisplayFormat("yyyy-MM-dd   HH:mm:ss");
  endTimeEdit->setDisplayFormat("yyyy-MM-dd   HH:mm:ss");

  auto *mainLay = new QVBoxLayout(this);

  auto *gridLay = new QGridLayout();
  gridLay->addWidget(new QLabel(tr("Начало диапазона:"), this), 0, 0);
  gridLay->addWidget(startTimeEdit, 0, 1);
  gridLay->addWidget(new QLabel(tr("Конец диапазона:"), this), 1, 0);
  gridLay->addWidget(endTimeEdit, 1, 1);

  auto *layBtns = new QHBoxLayout();
  layBtns->addWidget(cancelBtn);
  layBtns->addStretch();
  layBtns->addWidget(acceptBtn);

  mainLay->addLayout(gridLay);
  mainLay->addLayout(layBtns);

  {
    QFont font; //("Monospace")
    font.setBold(true);
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(20 * utils::ssf());
    startTimeEdit->setFont(font);
    endTimeEdit->setFont(font);
  }
  startTimeEdit->setFixedHeight(80);
  endTimeEdit->setFixedHeight(80);
  startTimeEdit->setStyleSheet(utils::readStyleFromFile(":/styles/dateTimeEdit.qss"));
  endTimeEdit->setStyleSheet(utils::readStyleFromFile(":/styles/dateTimeEdit.qss"));

  setWindowTitle(tr("Выбор временного диапазона"));
  this->adjustSize();
  setFixedSize(size());

  connect(acceptBtn, &QPushButton::clicked, this, [this, startTimeEdit, endTimeEdit]() {
    emit dateSelected(startTimeEdit->dateTime(), endTimeEdit->dateTime());
    accept();
  });

  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}
} // namespace deepvaccontrol
