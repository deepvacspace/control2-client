#include "advisorprogressdialog.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

#include "utils.h"
#include "theme.h"

namespace deepvaccontrol::ai {

AdvisorProgressDialog::AdvisorProgressDialog(QWidget *parent) : QDialog{parent} {
  setWindowTitle(tr("AI Advisor"));
  setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
  setWindowModality(Qt::ApplicationModal);

  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(theme::dp(40), theme::dp(32), theme::dp(40), theme::dp(32));
  lay->setSpacing(theme::dp(20));

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
  m_bar->setFixedHeight(theme::dp(15));
  lay->addWidget(m_bar);

  auto *buttons = new QHBoxLayout();
  buttons->setContentsMargins(0, theme::dp(16), 0, 0);
  buttons->addStretch();
  m_cancelBtn = new QPushButton(tr("Cancel"), this);
  m_cancelBtn->setMinimumSize(theme::dp(206), theme::dp(67));
  m_cancelBtn->setAutoDefault(false);
  buttons->addWidget(m_cancelBtn);
  lay->addLayout(buttons);
  connect(m_cancelBtn, &QPushButton::clicked, this, &AdvisorProgressDialog::reject);

  setProgress(0, 1);
  setFixedWidth(theme::dp(862));
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
