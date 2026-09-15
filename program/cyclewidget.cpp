#include "cyclewidget.h"

#include <QPushButton>
#include <QVBoxLayout>

#include "cycledialog.h"
#include "utils.h"

namespace deepvaccontrol {
CycleWidget::CycleWidget(QWidget *parent) : QWidget{parent}, m_intervalNumber{0U} {
  initGui();
}

void CycleWidget::initGui() {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_text = new QLabel(tr("нет"), this);
  m_text->setAlignment(Qt::AlignCenter);
  auto *editBtn = new QPushButton(tr("Изменить"), this);
  editBtn->setFixedHeight(50 * utils::ssf());

  mainLay->addWidget(m_text);
  mainLay->addWidget(editBtn);
  mainLay->setAlignment(m_text, Qt::AlignCenter);

  connect(editBtn, &QPushButton::clicked, this, [this] {
    auto *cycleDialog = new CycleDialog(m_params, m_intervalNumber, this);
    connect(cycleDialog, &QDialog::finished, this, &CycleWidget::updateGui);
    cycleDialog->open();
  });
}

void CycleWidget::setParams(QVariantMap const &newParams) {
  if (newParams == m_params) {
    return;
  }

  m_params = newParams;

  updateGui();
}

void CycleWidget::updateGui() {
  if (m_params.value("count").toInt() > 0) {
    QString cycles_number_string
      = (m_params.value("count").toInt() == 65535 ? tr("бесконечно")
                                                  : QString::number(m_params.value("count").toInt()) + " раз(а)");
    m_text->setText(QString("c %1 по %2\n%3")
                      .arg(m_params.value("start").toInt()+1)
                      .arg(m_params.value("end").toInt()+1)
                      .arg(cycles_number_string));
  } else {
    m_text->setText("None\n");
  }
  emit paramsChanged(m_params);
}

QVariantMap CycleWidget::emptyCycleMap() {
  QVariantMap out;
  out["start"] = 0;
  out["end"]   = 0;
  out["count"] = 0;
  return out;
}



void CycleWidget::setIntervalsCount(int i) {
  if (i < m_params.value("cycle_end").toInt()) {
    setParams(emptyCycleMap());
  }
  m_intervalNumber = i;
}

} // namespace deepvaccontrol
