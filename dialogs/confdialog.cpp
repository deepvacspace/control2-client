#include "confdialog.h"

#include "utils.h"

namespace control2 {
ConfDialog::ConfDialog(
  QString const &title, QString const &message, QString const &btn1Name, QString const &btn2Name, QWidget *parent)
    : MessageDialog{title, message, parent} {
  m_closeBtn->setVisible(false);

  auto *acceptBtn = new QPushButton(btn1Name);
  auto *denyBtn   = new QPushButton(btn2Name);

  auto f = acceptBtn->font();
  for (QPushButton *btn : {acceptBtn, denyBtn}) {
    btn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());
    f.setPointSize(12 * utils::ssf());
    btn->setFont(f);
    m_btnLayout->addWidget(btn);
  }

  connect(acceptBtn, &QPushButton::clicked, this, [this] {
    accept(); // return 1
  });
  connect(denyBtn, &QPushButton::clicked, this, [this] {
    reject(); // return 0
  });

  setAttribute(Qt::WA_DeleteOnClose);
}
} // namespace control2
