#include "advisorprogressdialog.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "utils.h"

namespace deepvaccontrol::ai {

AdvisorProgressDialog::AdvisorProgressDialog(QWidget *parent) : QDialog{parent} {
  double const ssf{utils::ssf()};
  setWindowTitle(tr("AI Advisor"));
  setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
  setWindowModality(Qt::ApplicationModal);

  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(22 * ssf, 18 * ssf, 22 * ssf, 18 * ssf);
  lay->setSpacing(10 * ssf);

  // title | percentage
  auto *head = new QHBoxLayout();
  m_title = new QLabel(tr("Calculating the best settings"), this);
  m_title->setObjectName("cardTitle");
  m_percent = new QLabel(this);
  m_percent->setObjectName("cardTitle");
  head->addWidget(m_title, 1);
  head->addWidget(m_percent);
  lay->addLayout(head);

  m_bar = new QProgressBar(this);
  m_bar->setObjectName("slimProgress");
  m_bar->setTextVisible(false);
  m_bar->setFixedHeight(static_cast<int>(8 * ssf));
  lay->addWidget(m_bar);

  auto *buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, 8 * ssf, 0, 0);
  buttons->addStretch();
  m_cancelBtn = new QPushButton(tr("Cancel"), this);
  m_cancelBtn->setMinimumSize(static_cast<int>(110 * ssf), static_cast<int>(36 * ssf));
  m_cancelBtn->setAutoDefault(false);
  buttons->addWidget(m_cancelBtn);
  lay->addLayout(buttons);
  connect(m_cancelBtn, &QPushButton::clicked, this, &AdvisorProgressDialog::reject);

  setProgress(0, 1);
  setFixedWidth(static_cast<int>(460 * ssf));
  adjustSize();
}

void AdvisorProgressDialog::setProgress(int done, int total) {
  m_bar->setRange(0, std::max(total, 1));
  m_bar->setValue(done);
  m_percent->setText(QStringLiteral("%1 %").arg(total > 0 ? (100 * done) / total : 0));
}

void AdvisorProgressDialog::finish() {
  m_finished = true;
  close();
}

void AdvisorProgressDialog::reject() {
  if (m_finished) {
    QDialog::reject();
    return;
  }
  if (!m_cancelling) {
    m_cancelling = true;
    m_title->setText(tr("Cancelling…"));
    m_cancelBtn->setEnabled(false);
    emit cancelRequested();
  }
}

void AdvisorProgressDialog::closeEvent(QCloseEvent *event) {
  if (m_finished) {
    QDialog::closeEvent(event);
    return;
  }
  event->ignore(); // the window's close button cancels; it closes once the calculation has stopped
  reject();
}

}
