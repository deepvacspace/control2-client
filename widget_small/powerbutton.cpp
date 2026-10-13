#include "powerbutton.h"

#include <QProcess>
#include <qprocess.h>

#include "confdialog.h"
#include "constants.h"
#include "messagedialog.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
PowerButton::PowerButton(ResHolder *resHolder, QWidget *parent) : QPushButton{QString{}, parent} {
  setText(tr("Off"));
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
        = new MessageDialog(tr("Warning"), tr("Stop operation before shutting down equipment"), this);
      msgDlg->open();
    } else {
      ConfDialog *confDialog
        = new ConfDialog(tr("Confirmation"),
                         tr("The chamber will be turned off and the program will be closed.\n\nDo you want to continue?"),
                         tr("Yes"),
                         tr("No"),
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
}
