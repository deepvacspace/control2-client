#include "settingsdiff.h"

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "utils.h"

namespace deepvaccontrol {
SettingsDiff::SettingsDiff(QMap<QString, float> const &local, QMap<QString, float> const &plc, QWidget *parent)
    : QDialog{parent}, m_local{local}, m_plc{plc} {
  // table / btnLay
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(3, 3, 3, 3);

  auto *table = new QTableWidget(this);
  table->setStyleSheet(utils::readStyleFromFile(":/styles/verticalScroll.qss"));
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  mainLay->addWidget(table);

  auto *btnLay = new QHBoxLayout();
  mainLay->addLayout(btnLay);

  auto *sendBtn = new QPushButton(tr("Загрузить текущие настройки"), this);
  auto *getBtn  = new QPushButton(tr("Принять настройки из ПЛК"), this);
  sendBtn->setMinimumHeight(40);
  getBtn->setMinimumHeight(40);
  btnLay->addWidget(getBtn);
  btnLay->addWidget(sendBtn);

  QMap<QString, float> diff;

  // Проверяем элементы из первого map
  for (auto it = local.constBegin(); it != local.constEnd(); ++it) {
    QString const &key    = it.key();
    float          value1 = it.value();

    if (!plc.contains(key)) {
      diff.insert(key, value1);
    } else if (!qFuzzyCompare(value1, plc.value(key))) {
      diff.insert(key, value1);
    }
  }

  // Проверяем элементы из второго map
  for (auto it = plc.constBegin(); it != plc.constEnd(); ++it) {
    QString const &key    = it.key();
    float          value2 = it.value();
    if (!local.contains(key)) {
      diff.insert(key, value2);
    }
  }

  table->setRowCount(diff.size());
  table->setColumnCount(3);
  table->setHorizontalHeaderLabels({tr("Настройка"), tr("ПЛК"), tr("Компьютер")});

  auto diffNames = diff.keys();
  int  uniquePlc{0};
  int  uniqueLocal{0};
  for (int i{0U}; i < diff.size(); ++i) {
    QString const &diffName{diffNames[i]};
    table->setItem(i, 0, new QTableWidgetItem(diffName));

    bool inPlc{plc.contains(diffName)};
    table->setItem(i, 1, new QTableWidgetItem(inPlc ? QString::number(plc[diffName]) : ""));

    bool inLocal{local.contains(diffName)};
    table->setItem(i, 2, new QTableWidgetItem(inLocal ? QString::number(local[diffName]) : ""));

    if (inPlc != inLocal) {
      table->item(i, 0)->setBackground(QBrush(QColor(255, 0, 0, 50)));
      table->item(i, 1)->setBackground(QBrush(QColor(255, 0, 0, 50)));
      table->item(i, 2)->setBackground(QBrush(QColor(255, 0, 0, 50)));
      auto font = table->itemAt(i, 0)->font();
      font.setBold(true);
      table->item(i, 0)->setFont(font);
      table->item(i, 1)->setFont(font);
      table->item(i, 2)->setFont(font);
      uniquePlc += static_cast<int>(inPlc);
      uniqueLocal += static_cast<int>(inLocal);
    } else {
      table->item(i, 0)->setBackground(QBrush(QColor(255, 255, 0, 50)));
      table->item(i, 1)->setBackground(QBrush(QColor(255, 255, 0, 50)));
      table->item(i, 2)->setBackground(QBrush(QColor(255, 255, 0, 50)));
    }
  }

  if (uniqueLocal > 0) {
    getBtn->setEnabled(false);
  }
  if (uniquePlc > 0) {
    sendBtn->setEnabled(false);
    getBtn->setEnabled(false);
  }

  connect(sendBtn, &QPushButton::clicked, this, [this]() {
    done(2);
  });

  connect(getBtn, &QPushButton::clicked, this, [this]() {
    done(3);
  });

  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Отличающиеся настройки"));
  setMinimumSize(410, 450);
}
} // namespace deepvaccontrol
