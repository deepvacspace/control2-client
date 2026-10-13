#include "connectiondialog.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "constants.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
ConnectionDialog::ConnectionDialog(QString const &serverId, QWidget *parent) : QDialog{parent}, m_serverId{serverId} {
  initGui();

  // connect(m_dialog, &QMessageBox::buttonClicked, this, &ConnectionDialog::handleBtnClicked);
  // m_dialog->installEventFilter(this);
}

void ConnectionDialog::initGui() {
  double const ssf{utils::ssf()};
  setWindowTitle(tr("Connection"));
  setWindowFlag(Qt::WindowStaysOnTopHint);
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);

  // an icon beside the title and the message; the button at the bottom right
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(22 * ssf, 20 * ssf, 22 * ssf, 18 * ssf);
  mainLay->setSpacing(16 * ssf);
  auto *top = new QHBoxLayout();
  top->setSpacing(14 * ssf);
  m_iconLabel = new QLabel(this);
  m_iconLabel->setPixmap(theme::dialogIcon(QStringLiteral("error"), static_cast<int>(40 * ssf)));
  m_iconLabel->setAlignment(Qt::AlignTop);
  top->addWidget(m_iconLabel, 0, Qt::AlignTop);
  auto *text = new QVBoxLayout();
  text->setSpacing(4 * ssf);
  m_titleLabel = new QLabel(this);
  QFont font   = m_titleLabel->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 1.3);
  m_titleLabel->setFont(font);
  m_msgLabel = new QLabel(this);
  m_msgLabel->setWordWrap(true);
  m_attemptLabel = new QLabel(this);
  m_attemptLabel->setProperty("muted", true);
  m_hintLabel = new QLabel(tr("Check that the chamber is switched on and connected to the network, "
                              "or choose another chamber in Settings → TCP."),
                           this);
  m_hintLabel->setProperty("muted", true);
  m_hintLabel->setWordWrap(true);
  text->addWidget(m_titleLabel);
  text->addWidget(m_msgLabel);
  text->addSpacing(6 * ssf);
  text->addWidget(m_attemptLabel);
  text->addWidget(m_hintLabel);
  top->addLayout(text, 1);
  mainLay->addLayout(top);

  auto *buttons = new QHBoxLayout();
  buttons->addStretch();
  m_hideBtn = new QPushButton(tr("Hide"), this);
  m_hideBtn->setObjectName("primaryButton");
  m_hideBtn->setMinimumSize(110 * ssf, 34 * ssf);
  m_hideBtn->setToolTip(tr("The application keeps trying to connect in the background"));
  buttons->addWidget(m_hideBtn);
  mainLay->addLayout(buttons);

  setFixedWidth(static_cast<int>(460 * ssf));
  updateContent(1, 0);

  // (x) close = done(0)
  connect(m_hideBtn, &QPushButton::clicked, this, [this]() {
    hide();
    done(1);
  });
}

void ConnectionDialog::updateContent(int contTryCount, int successCount) {
  bool const lost{successCount > 0};
  m_titleLabel->setText(lost ? tr("Connection to the chamber lost") : tr("No connection to the chamber"));
  m_msgLabel->setText(lost ? tr("The connection to %1 was interrupted.").arg(m_serverId)
                           : tr("%1 cannot be reached.").arg(m_serverId));
  m_attemptLabel->setText(tr("Trying again… attempt %1").arg(contTryCount));
  adjustSize();
}

void ConnectionDialog::reset() {
  hide();
}

void ConnectionDialog::setServerId(QString const &serverId) {
  m_serverId = serverId;
}

// void ConnectionDialog::resetOfflineWork() {
//   m_isOfflineWorkSelected = false;
// }

// void ConnectionDialog::handleBtnClicked(QAbstractButton *button) {
//   if (m_dialogCloseBtn == button) {
//     handleDialogClose();
//   }
//   if (m_dialogOfflineBtn == button) {
//     m_isOfflineWorkSelected = true;
//     emit offlineWorkSelected();
//     qDebug() << "User accept work without connection, m_isOfflineWorkSelected = true";
//   }
// }

// void ConnectionDialog::handleDialogClose() {
//   m_dialog->hide();
// }

void ConnectionDialog::closeEvent(QCloseEvent *) {
  emit finished(0); // QDialog::Rejected == close dialog
}
} // namespace deepvaccontrol
