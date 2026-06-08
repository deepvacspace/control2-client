#include "powerbutton.h"

#include <QProcess>
#include <qprocess.h>

#include "confdialog.h"
#include "constants.h"
#include "messagedialog.h"
#include "utils.h"

namespace control2 {
PowerButton::PowerButton(ResHolder *resHolder, QWidget *parent) : QPushButton{"Выкл.", parent} {
  setText(tr("Выкл"));
  setFixedHeight(control2::UI_MENU_BTN_HEIGHT * utils::ssf());

  setIcon(QIcon(":/imgs/power.png"));
  setIconSize(QSize(30 * utils::ssf(), 30 * utils::ssf()));
  {
    auto ft = font();
    ft.setPointSize(ft.pointSize() * 1.5);
    ft.setBold(true);
    setFont(ft);
  }

  connect(this, &QPushButton::clicked, this, [this, resHolder] {
    if (HardwareStatus::HardwareStatusEnum::WORK == resHolder->hard_status->getHardwareStatus()) {
      auto *msgDlg
        = new MessageDialog(tr("Предупреждение"), tr("Для выключения оборудования необходимо остановить работу"), this);
      msgDlg->open();
    } else {
      ConfDialog *confDialog
        = new ConfDialog(tr("Подтверждение"),
                         tr("Вы действительно хотите выключить компьютер?\nРаботы камеры будет остановлена."),
                         tr("Да"),
                         tr("Нет"),
                         this);
      connect(confDialog, &ConfDialog::finished, this, [this, resHolder](int result) {
        if (1 == result) {
          resHolder->tcpGate->sendTextCmd("shutdown");
          emit prepareToCloseApp();
          system("c:\\windows\\system32\\shutdown /s /t 1");
        }
      });
      confDialog->show();
    }
  });
}
} // namespace control2
