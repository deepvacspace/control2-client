#include "powerbutton.h"

#include <QProcess>
#include <qprocess.h>

#include "confdialog.h"
#include "constants.h"
#include "messagedialog.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
PowerButton::PowerButton(ResHolder *resHolder, QWidget *parent) : QPushButton{"Выкл.", parent} {
  setText(tr("Выкл"));
  setObjectName("offButton");
  setFixedSize(92 * utils::ssf(), 34 * utils::ssf());
  QColor const danger = theme::colors().danger;
  setIcon(theme::menuIcon(QStringLiteral("power"), danger, danger, static_cast<int>(13 * utils::ssf())));
  setIconSize(QSize(13 * utils::ssf(), 13 * utils::ssf()));
  setStyleSheet(QStringLiteral("QPushButton#offButton { color: %1; border: %2px solid %1; background: transparent; "
                               "border-radius: %3px; } QPushButton#offButton:pressed { background: %4; }")
                  .arg(danger.name())
                  .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                  .arg(static_cast<int>(5 * utils::ssf()))
                  .arg(QColor(danger.red(), danger.green(), danger.blue(), 50).name(QColor::HexArgb)));

  connect(this, &QPushButton::clicked, this, [this, resHolder] {
    if (HardwareStatus::HardwareStatusEnum::WORK == resHolder->hard_status->getHardwareStatus()) {
      auto *msgDlg
        = new MessageDialog(tr("Предупреждение"), tr("Для выключения оборудования необходимо остановить работу"), this);
      msgDlg->open();
    } else {
      ConfDialog *confDialog
        = new ConfDialog(tr("Подтверждение"),
                         tr("The chamber will be turned off and the program will be closed.\n\nThe Windows computer will stay on.\n\nDo you want to continue?"),
                         tr("Да"),
                         tr("Нет"),
                         this);
      connect(confDialog, &ConfDialog::finished, this, [this, resHolder](int result) {
        if (1 == result) {
          resHolder->tcpGate->sendTextCmd("shutdown");
          emit prepareToCloseApp();
          // system("c:\\windows\\system32\\shutdown /s /t 1");
        }
      });
      confDialog->show();
    }
  });
}
} // namespace deepvaccontrol
