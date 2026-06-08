#include "connectiondialog.h"

#include <QEvent>
#include <QVBoxLayout>

#include "constants.h"
#include "utils.h"

namespace control2 {
ConnectionDialog::ConnectionDialog(QString const &serverId, QWidget *parent) : QDialog{parent}, m_serverId{serverId} {
  initGui();

  // connect(m_dialog, &QMessageBox::buttonClicked, this, &ConnectionDialog::handleBtnClicked);
  // m_dialog->installEventFilter(this);
}

void ConnectionDialog::initGui() {
  auto *mainLay = new QVBoxLayout(this);

  m_titleLabel = new QLabel(tr("Ошибка соединения"), this);
  mainLay->addWidget(m_titleLabel);

  auto *fr = new QFrame();
  fr->setFrameStyle(QFrame::HLine | QFrame::Plain);
  fr->setLineWidth(1);
  fr->setFixedWidth(350 * utils::ssf());
  mainLay->addWidget(fr);
  mainLay->addSpacing(15 * utils::ssf());

  m_msgLabel = new QLabel(this);
  mainLay->addWidget(m_msgLabel);
  mainLay->addSpacing(30 * utils::ssf());
  mainLay->addStretch();

  auto *btnLayout = new QVBoxLayout();
  mainLay->addLayout(btnLayout);

  m_hideBtn = new QPushButton(tr("Скрыть уведомления\n(продолжить попытки подключения)"), this);
  // m_workOfflineBtn = new QPushButton(tr("Работать офлайн"), this);
  // m_cancelBtn      = new QPushButton(tr("Завершить работу"), this);
  btnLayout->addWidget(m_hideBtn);
  // btnLayout->addWidget(m_workOfflineBtn);
  // btnLayout->addWidget(m_cancelBtn);

  mainLay->setAlignment(m_titleLabel, Qt::AlignHCenter);
  mainLay->setAlignment(fr, Qt::AlignHCenter);
  mainLay->setAlignment(m_msgLabel, Qt::AlignHCenter);

  auto f = m_titleLabel->font();
  f.setPointSize(20 * utils::ssf());
  m_titleLabel->setFont(f);
  f.setPointSize(12 * utils::ssf());
  m_msgLabel->setFont(f);
  m_hideBtn->setFont(f);
  // m_workOfflineBtn->setFont(f);
  // m_cancelBtn->setFont(f);

  m_hideBtn->setFixedHeight(1.5 * control2::UI_BTN_HEIGHT * utils::ssf());
  // m_workOfflineBtn->setFixedHeight(control2::UI_BTN_HEIGHT * utils::ssf());
  // m_cancelBtn->setFixedHeight(control2::UI_BTN_HEIGHT * utils::ssf());

  setWindowTitle(m_titleLabel->text());
  setWindowFlag(Qt::WindowStaysOnTopHint);

  adjustSize();
  setFixedSize(size());

  // (x) close = done(0)
  connect(m_hideBtn, &QPushButton::clicked, this, [this]() {
    hide();
    done(1);
  });
  // connect(m_workOfflineBtn, &QPushButton::clicked, this, [this]() {
  //   done(2);
  // });
  // connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
  //   done(3);
  // });
}

void ConnectionDialog::updateContent(int contTryCount, int successCount) {
  QString msg;
  if (0 == successCount) {
    msg = QString(tr("Не удаётся подключиться к %1.\nПопытка подключения #%2.")).arg(m_serverId).arg(contTryCount);
  } else {
    msg = QString(tr("Соединение c %1 было потеряно.\nПопытка переподключения #%2.")).arg(m_serverId).arg(contTryCount);
  }

  m_msgLabel->setText(msg);
}

void ConnectionDialog::reset() {
  hide();
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
} // namespace control2
