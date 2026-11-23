#include "cycledialog.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "lineeditnum.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
constexpr quint32 const INFINITY_CYCLE_COUNT{65535};

CycleDialog::CycleDialog(QVariantMap &cycleParams, int interval, QWidget *parent)
    : QDialog{parent}, m_cycleParams{cycleParams} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(theme::dp(8), theme::dp(8), theme::dp(8), theme::dp(8));

  auto *lay1 = new QHBoxLayout();
  auto *lay2 = new QHBoxLayout();
  auto *lay3 = new QHBoxLayout();

  auto *label1 = new QLabel(tr("Start interval:"));
  auto *label2 = new QLabel(tr("End interval:"));
  auto *label3 = new QLabel(tr("Repeat count:"));

  auto *startIndexEdit  = new LineEditNum(1, interval, 0, true, this);
  auto *endIndexEdit    = new LineEditNum(1, interval, 0, true, this);
  auto *cycleNumberEdit = new LineEditNum(2, 1000, 0, true, this);

  startIndexEdit->setFixedWidth(theme::dp(131));
  endIndexEdit->setFixedWidth(theme::dp(131));
  cycleNumberEdit->setFixedWidth(theme::dp(131));

  auto *infinity = new QCheckBox(tr("Infinite repeat"));
  infinity->setChecked(false);

  // Fill fields
  if (cycleParams["count"].toUInt() > 1) {
    startIndexEdit->setText(QString::number(cycleParams["start"].toUInt() + 1));
    endIndexEdit->setText(QString::number(cycleParams["end"].toUInt() + 1));
    cycleNumberEdit->setText(QString::number(cycleParams["count"].toUInt()));

    if (deepvaccontrol::INFINITY_CYCLE_COUNT == cycleParams["count"].toUInt()) {
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

  auto *cancelBtn = new QPushButton(tr("Cancel"), this);
  auto *acceptBtn = new QPushButton(tr("Save"), this);
  auto *delBtn    = new QPushButton(tr("Delete"), this);

  cancelBtn->setFixedSize(theme::dp(225), theme::dp(93));
  acceptBtn->setFixedSize(theme::dp(225), theme::dp(93));
  delBtn->setFixedSize(theme::dp(225), theme::dp(93));

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

  setMinimumHeight(theme::dp(187));
  setWindowTitle(tr("Edit cycle"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlag(Qt::WindowStaysOnTopHint);
  setModal(true);
  adjustSize();
}
}
