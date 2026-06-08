#include "cycledialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "lineeditnum.h"
#include "utils.h"

namespace control2 {
constexpr quint32 const INFINITY_CYCLE_COUNT{65535};

CycleDialog::CycleDialog(QVariantMap &cycleParams, int interval, QWidget *parent)
    : QDialog{parent}, m_cycleParams{cycleParams} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(5 * utils::ssf(), 5 * utils::ssf(), 5 * utils::ssf(), 5 * utils::ssf());

  auto *lay1 = new QHBoxLayout();
  auto *lay2 = new QHBoxLayout();
  auto *lay3 = new QHBoxLayout();

  auto *label1 = new QLabel(tr("Начальный интервал:"));
  auto *label2 = new QLabel(tr("Конечный интервал:"));
  auto *label3 = new QLabel(tr("Количество повтореий:"));

  auto *startIndexEdit  = new LineEditNum(1, interval, 0, true, this);
  auto *endIndexEdit    = new LineEditNum(1, interval, 0, true, this);
  auto *cycleNumberEdit = new LineEditNum(2, 1000, 0, true, this);

  startIndexEdit->setFixedWidth(70 * utils::ssf());
  endIndexEdit->setFixedWidth(70 * utils::ssf());
  cycleNumberEdit->setFixedWidth(70 * utils::ssf());

  auto *infinity = new QCheckBox(tr("Бесконечное потоврение"));
  infinity->setChecked(false);

  // Fill fields
  if (cycleParams["count"].toUInt() > 1) {
    startIndexEdit->setText(QString::number(cycleParams["start"].toUInt() + 1));
    endIndexEdit->setText(QString::number(cycleParams["end"].toUInt() + 1));
    cycleNumberEdit->setText(QString::number(cycleParams["count"].toUInt()));

    if (control2::INFINITY_CYCLE_COUNT == cycleParams["count"].toUInt()) {
      cycleNumberEdit->setText("3");
      cycleNumberEdit->setEnabled(false);
      infinity->setChecked(true);
    }
  } else {
    startIndexEdit->setText("1");
    endIndexEdit->setText(QString::number(interval));
    cycleNumberEdit->setText("3");
  }

  connect(startIndexEdit, &LineEditNum::userEditingFinished, this, [endIndexEdit, startIndexEdit, interval]() {
    endIndexEdit->setMinMaxPrec(startIndexEdit->text().toInt(), interval, 0);
  });

  connect(endIndexEdit, &LineEditNum::userEditingFinished, this, [endIndexEdit, startIndexEdit]() {
    startIndexEdit->setMinMaxPrec(1, endIndexEdit->text().toInt(), 0);
  });

  connect(infinity, &QCheckBox::toggled, this, [cycleNumberEdit](bool value) {
    cycleNumberEdit->setEnabled(!value);
  });

  auto *cancelBtn = new QPushButton(tr("Отмена"), this);
  auto *acceptBtn = new QPushButton(tr("Сохранить"), this);
  auto *delBtn    = new QPushButton(tr("Удалить"), this);

  cancelBtn->setFixedSize(120 * utils::ssf(), 50 * utils::ssf());
  acceptBtn->setFixedSize(120 * utils::ssf(), 50 * utils::ssf());
  delBtn->setFixedSize(120 * utils::ssf(), 50 * utils::ssf());

  connect(cancelBtn, &QPushButton::clicked, this, &CycleDialog::reject);

  connect(delBtn, &QPushButton::clicked, this, [this] {
    m_cycleParams["start"] = static_cast<quint32>(0U);
    m_cycleParams["end"]   = static_cast<quint32>(0U);
    m_cycleParams["count"] = static_cast<quint32>(0U);
    accept();
  });

  connect(acceptBtn, &QPushButton::clicked, this, [this, startIndexEdit, endIndexEdit, infinity, cycleNumberEdit] {
    m_cycleParams["start"] = startIndexEdit->text().toUInt() - 1;
    m_cycleParams["end"]   = endIndexEdit->text().toUInt() - 1;
    m_cycleParams["count"] = (infinity->isChecked() ? INFINITY_CYCLE_COUNT : cycleNumberEdit->text().toUInt());
    accept();
  });

  lay1->addStretch();
  lay1->addWidget(label1);
  lay1->addWidget(startIndexEdit);
  lay1->addStretch();
  lay1->addWidget(label2);
  lay1->addWidget(endIndexEdit);
  lay1->addStretch();

  lay2->addStretch();
  lay2->addWidget(label3);
  lay2->addWidget(cycleNumberEdit);
  lay2->addWidget(infinity);
  lay2->addStretch();

  lay3->addWidget(delBtn);
  lay3->addStretch();
  lay3->addWidget(cancelBtn);
  lay3->addWidget(acceptBtn);

  mainLay->addLayout(lay1);
  mainLay->addLayout(lay2);
  mainLay->addStretch();
  mainLay->addLayout(lay3);

  setStyleSheet(utils::readStyleFromFile(":/styles/checkBoxBig.qss")
                + QString("QCheckBox::indicator{width: %1px; height: %1px;}").arg(26 * utils::ssf()));

  setMinimumHeight(100 * utils::ssf());
  setWindowTitle(tr("Изменение цикла"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlag(Qt::WindowStaysOnTopHint);
  setModal(true);
  adjustSize();
}
} // namespace control2
