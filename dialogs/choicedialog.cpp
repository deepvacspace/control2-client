#include "choicedialog.h"

#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {

ChoiceDialog::ChoiceDialog(QString const &title, QString const &message, QString const &icon, QWidget *parent)
    : MessageDialog{title, message, parent} {
  m_closeBtn->setVisible(false);
  setIconKind(icon);
}

QPushButton *ChoiceDialog::addChoice(QString const &text, int code, bool primary) {
  auto *button = new QPushButton(text, this);
  button->setMinimumSize(theme::dp(206), theme::dp(67));
  if (primary) {
    button->setObjectName("primaryButton");
    button->setDefault(true);
  } else {
    button->setAutoDefault(false);
  }
  m_btnLayout->addWidget(button);
  connect(button, &QPushButton::clicked, this, [this, code] { done(code); });
  adjustSize();
  return button;
}

}
