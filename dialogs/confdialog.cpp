#include "confdialog.h"

#include "utils.h"

namespace deepvaccontrol {
ConfDialog::ConfDialog(
  QString const &title, QString const &message, QString const &btn1Name, QString const &btn2Name, QWidget *parent)
    : MessageDialog{title, message, parent} {
  m_closeBtn->setVisible(false);
  setIconKind(QStringLiteral("question"));

  // the answer that goes ahead on the right, highlighted
  auto *acceptBtn = new QPushButton(btn1Name, this);
  auto *denyBtn   = new QPushButton(btn2Name, this);
  acceptBtn->setObjectName("primaryButton");
  for (QPushButton *btn : {denyBtn, acceptBtn}) {
    btn->setMinimumSize(static_cast<int>(110 * utils::ssf()), static_cast<int>(36 * utils::ssf()));
    m_btnLayout->addWidget(btn);
  }
  acceptBtn->setDefault(true);

  connect(acceptBtn, &QPushButton::clicked, this, [this] {
    accept(); // return 1
  });
  connect(denyBtn, &QPushButton::clicked, this, [this] {
    reject(); // return 0
  });

  setAttribute(Qt::WA_DeleteOnClose);
}
}
