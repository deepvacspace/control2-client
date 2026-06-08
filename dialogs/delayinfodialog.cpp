#include "delayinfodialog.h"

#include <QPushButton>
#include <QVBoxLayout>

#include "utils.h"

namespace control2 {
DelayInfoDialog::DelayInfoDialog(QString const &desription, QDateTime startTime, QWidget *parent)
    : QDialog{parent}, m_startTime{startTime} {
  auto *mainLay = new QVBoxLayout(this);

  auto *cancelBtn = new QPushButton(tr("Отмена"), this);
  cancelBtn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());

  auto *timeLabelStart = new QLabel(this);
  m_timeLabelCurrent   = new QLabel(this);
  auto *labelDesc      = new QLabel(desription, this);
  auto *labelTitle     = new QLabel(tr("Отложенный пуск"), this);

  {
    QFont font = timeLabelStart->font();
    font.setPointSize(12);
    timeLabelStart->setFont(font);
    m_timeLabelCurrent->setFont(font);
    cancelBtn->setFont(font);
    font.setPointSize(20 * utils::ssf());
    labelTitle->setFont(font);
  }

  auto *fr = new QFrame();
  fr->setFrameStyle(QFrame::HLine | QFrame::Plain);
  fr->setLineWidth(1);
  fr->setFixedWidth(350 * utils::ssf());

  auto *gridLay = new QGridLayout(this);
  gridLay->addWidget(new QLabel(tr("Текущее время:"), this), 0, 0);
  gridLay->addWidget(m_timeLabelCurrent, 0, 1);
  gridLay->addWidget(new QLabel(tr("Время пуска:"), this), 1, 0);
  gridLay->addWidget(timeLabelStart, 1, 1);

  auto *labelLay = new QHBoxLayout();
  labelLay->addStretch();
  labelLay->addLayout(gridLay);
  labelLay->addStretch();
  mainLay->addWidget(labelTitle);
  mainLay->addWidget(fr);
  mainLay->addSpacing(15 * utils::ssf());
  mainLay->addLayout(labelLay);
  mainLay->addSpacing(15 * utils::ssf());
  mainLay->addWidget(labelDesc);
  mainLay->addStretch();
  mainLay->addSpacing(15 * utils::ssf());
  mainLay->addWidget(cancelBtn);

  QList<QWidget *> list1{labelTitle, fr, timeLabelStart, m_timeLabelCurrent, labelDesc, cancelBtn};
  for (auto &item : list1) {
    mainLay->setAlignment(item, Qt::AlignHCenter);
  }

  timeLabelStart->setText(startTime.toString("yyyy-MM-dd HH:mm:ss"));
  m_timeLabelCurrent->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));

  m_timer = new QTimer(this);
  connect(m_timer, &QTimer::timeout, this, &DelayInfoDialog::checkTime);
  m_timer->start(1000);

  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  this->setWindowTitle(tr("Отложенный пуск"));
  this->setAttribute(Qt::WA_DeleteOnClose);
  //    this->setModal(true);
  this->adjustSize();
  setFixedSize(size());
}

void DelayInfoDialog::checkTime() {
  m_timeLabelCurrent->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));

  if (QDateTime::currentDateTime() >= m_startTime) {
    m_timer->stop();
    accept();
  }
}
} // namespace control2
