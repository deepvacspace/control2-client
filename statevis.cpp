#include "statevis.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>

#include "labelplus.h"
#include "utils.h"

namespace control2 {
StateVis::StateVis(QString const &name, SystemValue *x, SystemValue *r, SystemValue *u, double sf, QWidget *parent)
    : QWidget{parent} {
  if (sf < 1) {
    sf = 1;
  }
  auto *xVal = new LabelPlus(x, this);
  xVal->setFont(QFont("MS Shell Dlg 2", 20 * sf * utils::ssf(), QFont::Bold));
  auto *rVal = new LabelPlus(r, this);
  rVal->setFont(QFont("MS Shell Dlg 2", 6 * sf * utils::ssf()));
  auto *uVal = new LabelPlus(u, this);
  uVal->setFont(QFont("MS Shell Dlg 2", 6 * sf * utils::ssf()));

  auto *mainLay = new QHBoxLayout(this);
  auto *box     = new QGroupBox(name, this);
  auto *lay     = new QGridLayout(box);

  mainLay->addWidget(box);
  lay->addWidget(xVal, 0, 0, 1, 2, Qt::AlignHCenter | Qt::AlignVCenter);
  auto *newLabel1 = new QLabel(tr("Уставка:"), this);
  auto *newLabel2 = new QLabel(tr("Управление, %:"), this);
  newLabel1->setFont(QFont("MS Shell Dlg 2", 6 * (1 + (sf - 1) * 0.25) * utils::ssf()));
  newLabel2->setFont(QFont("MS Shell Dlg 2", 6 * (1 + (sf - 1) * 0.25) * utils::ssf()));
  lay->addWidget(newLabel1, 1, 0, Qt::AlignVCenter);
  lay->addWidget(rVal, 1, 1, Qt::AlignVCenter);
  lay->addWidget(newLabel2, 2, 0, Qt::AlignVCenter);
  lay->addWidget(uVal, 2, 1, Qt::AlignVCenter);

  setMinimumWidth((x->isSciNotation() ? 270 : 180) * (1 + (sf - 1) * 0.25) * utils::ssf());
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
} // namespace control2
