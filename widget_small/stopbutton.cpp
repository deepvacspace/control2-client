#include "stopbutton.h"

#include "confdialog.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
StopButton::StopButton(QWidget *parent) : QPushButton{parent} {
  setText(tr("Stop"));
  setObjectName("stopButton"); // red in the application style sheet
  setFixedSize(theme::dp(187), theme::dp(93));
  setFont(QFont("MS Shell Dlg 2", 12 * utils::ssf(), 75));
  // setEnabled(false);

  connect(this, &QPushButton::clicked, this, [this] {
    auto *confDialog = new ConfDialog(
      tr("Confirmation"), tr("Do you really want to stop operation?"), tr("Yes"), tr("No"), this);
    connect(confDialog, &ConfDialog::finished, this, [&](int result) {
      if (1 == result) { // == accepted()
        emit confirmPressed();
      }
    });
    confDialog->open();
  });
}
}
