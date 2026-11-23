#include "alarmdialog.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
AlarmDialog::AlarmDialog(QWidget *parent) : QDialog{parent} {
  auto *mainLay = new QVBoxLayout(this);

  auto *titleLabel = new QLabel(tr("Hardware fault"), this);
  mainLay->addWidget(titleLabel);

  auto *fr = new QFrame(this);
  fr->setFrameStyle(QFrame::HLine | QFrame::Plain);
  fr->setLineWidth(1);
  fr->setFixedWidth(theme::dp(656));
  mainLay->addWidget(fr);
  mainLay->addSpacing(theme::dp(28));

  m_alarmsListLabel = new QLabel(tr("Alarm list"), this);
  mainLay->addWidget(m_alarmsListLabel);
  mainLay->addSpacing(theme::dp(56));
  mainLay->addStretch();

  auto *btnsLayout = new QHBoxLayout();

  auto *closeBtn = new QPushButton(tr("Close"), this);
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

  closeBtn->setFixedSize(theme::dp(187), theme::dp(93));

  setWindowTitle(tr("Hardware fault"));
  setWindowFlag(Qt::WindowStaysOnTopHint);

  //    setFixedWidth(theme::dp(750);
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
}
