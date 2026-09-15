#include "messagedialog.h"

#include <QLabel>
#include <QTimer>

#include "utils.h"

namespace deepvaccontrol {
MessageDialog::MessageDialog(QString const &title, QString const &message, QWidget *parent) : QDialog{parent} {
  auto *mainLay = new QVBoxLayout(this);

  auto *titleLabel = new QLabel(title);
  mainLay->addWidget(titleLabel);

  auto *fr = new QFrame();
  fr->setFrameStyle(QFrame::HLine | QFrame::Plain);
  fr->setLineWidth(1);
  fr->setFixedWidth(350 * utils::ssf());
  mainLay->addWidget(fr);
  mainLay->addSpacing(15 * utils::ssf());

  auto *msgLabel = new QLabel(message);
  mainLay->addWidget(msgLabel);
  mainLay->addSpacing(30 * utils::ssf());
  mainLay->addStretch();

  m_btnLayout = new QHBoxLayout();

  m_closeBtn = new QPushButton(tr("Закрыть"), this);
  m_btnLayout->addWidget(m_closeBtn);

  mainLay->addLayout(m_btnLayout);

  mainLay->setAlignment(titleLabel, Qt::AlignHCenter);
  mainLay->setAlignment(fr, Qt::AlignHCenter);
  mainLay->setAlignment(msgLabel, Qt::AlignHCenter);

  auto f = titleLabel->font();
  f.setPointSize(20 * utils::ssf());
  titleLabel->setFont(f);
  f.setPointSize(12 * utils::ssf());
  msgLabel->setFont(f);
  m_closeBtn->setFont(f);

  m_closeBtn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());

  setWindowTitle(title);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlag(Qt::WindowStaysOnTopHint);

  //    setFixedWidth(400*utils::ssf());
  adjustSize();
  setFixedSize(size());

  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

void MessageDialog::closeEvent(QCloseEvent *) {
  emit finished(0); // QDialog::Rejected == close dialog
}
} // namespace deepvaccontrol

void deepvaccontrol::MessageDialog::open(int visDurationMs) {
  QDialog::open();

  if (0 != visDurationMs) {
    m_closeTimer = new QTimer(this);
    m_closeTimer->setSingleShot(true);
    connect(m_closeTimer, &QTimer::timeout, this, [this]() {
      done(0);
    });
    m_closeTimer->start(visDurationMs);
  }
}
