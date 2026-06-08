#include "alarmdialog.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "utils.h"

namespace control2 {
AlarmDialog::AlarmDialog(QWidget *parent) : QDialog{parent} {
  auto *mainLay = new QVBoxLayout(this);

  auto *titleLabel = new QLabel(tr("Аппаратная ошибка"), this);
  mainLay->addWidget(titleLabel);

  auto *fr = new QFrame(this);
  fr->setFrameStyle(QFrame::HLine | QFrame::Plain);
  fr->setLineWidth(1);
  fr->setFixedWidth(350 * utils::ssf());
  mainLay->addWidget(fr);
  mainLay->addSpacing(15 * utils::ssf());

  m_alarmsListLabel = new QLabel(tr("Список аварий"), this);
  mainLay->addWidget(m_alarmsListLabel);
  mainLay->addSpacing(30 * utils::ssf());
  mainLay->addStretch();

  auto *btnsLayout = new QHBoxLayout();

  auto *closeBtn = new QPushButton(tr("Закрыть"), this);
  btnsLayout->addWidget(closeBtn);

  mainLay->addLayout(btnsLayout);

  mainLay->setAlignment(titleLabel, Qt::AlignHCenter);
  mainLay->setAlignment(fr, Qt::AlignHCenter);
  mainLay->setAlignment(m_alarmsListLabel, Qt::AlignHCenter);

  {
    auto font = titleLabel->font();
    font.setPointSize(20 * utils::ssf());
    titleLabel->setFont(font);
    font.setPointSize(12 * utils::ssf());
    m_alarmsListLabel->setFont(font);
    closeBtn->setFont(font);
  }

  closeBtn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());

  setWindowTitle(tr("Аппаратная ошибка"));
  setWindowFlag(Qt::WindowStaysOnTopHint);

  //    setFixedWidth(400*utils::ssf());
  adjustSize();
  setFixedSize(size());

  connect(closeBtn, &QPushButton::clicked, this, &AlarmDialog::onClose);
}

void AlarmDialog::showAlarmsList(QStringList alarmLabelsList) {
  m_alarmsListLabel->setText(alarmLabelsList.join("\n"));
  show();
}

void AlarmDialog::onClose() {
  emit accepted();
  hide();
}

void AlarmDialog::closeEvent(QCloseEvent *event) {
  onClose();
  // Proceed with the regular close event
  event->accept();
}
} // namespace control2
