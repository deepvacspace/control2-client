#include "stopbutton.h"

#include "confdialog.h"
#include "utils.h"

namespace control2 {
StopButton::StopButton(QWidget *parent) : QPushButton{parent} {
  setText(tr("Стоп"));
  setFixedSize(100 * utils::ssf(), 50 * utils::ssf());
  setFont(QFont("MS Shell Dlg 2", 12 * utils::ssf(), 75));
  // setEnabled(false);

  connect(this, &QPushButton::clicked, this, [this] {
    auto *confDialog = new ConfDialog(
      tr("Подтверждение"), tr("Вы действительно хотите остановить работу?"), tr("Да"), tr("Нет"), this);
    connect(confDialog, &ConfDialog::finished, this, [&](int result) {
      if (1 == result) { // == accepted()
        emit confirmPressed();
      }
    });
    confDialog->open();
  });
}
} // namespace control2
