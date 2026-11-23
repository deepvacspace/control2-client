#include "cyclewidget.h"

#include <QHBoxLayout>
#include <QPushButton>

#include "cycledialog.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
CycleWidget::CycleWidget(QWidget *parent) : QWidget{parent}, m_intervalNumber{0U} {
  initGui();
}

void CycleWidget::initGui() {
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_text = new QLabel(tr("none"), this);
  QFont font = m_text->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 1.15);
  m_text->setFont(font);
  auto *editBtn = new QPushButton(tr("Edit"), this);
  editBtn->setFixedSize(theme::dp(150), theme::dp(48));

  mainLay->addWidget(m_text, 1);
  mainLay->addWidget(editBtn);

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
      = (m_params.value("count").toInt() == 65535 ? tr("infinite")
                                                  : tr("%1").arg(m_params.value("count").toInt()));
    m_text->setText(tr("Intervals %1 – %2  ×  %3")
                      .arg(m_params.value("start").toInt()+1)
                      .arg(m_params.value("end").toInt()+1)
                      .arg(cycles_number_string));
  } else {
    m_text->setText(tr("None") + QStringLiteral("\n"));
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

}
