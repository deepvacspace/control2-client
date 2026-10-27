#include "powerbutton.h"

#include <QProcess>
#include <qprocess.h>

#include "choicedialog.h"
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
    // the chamber turned off with the application, or left as it is (a running job goes on without the application)
    bool const working{HardwareStatus::HardwareStatusEnum::WORK == resHolder->hard_status->getHardwareStatus()};
    enum { CANCEL = 1, APP_ONLY = 2, STOP_AND_EXIT = 3 };
    auto *dialog = new ChoiceDialog(tr("Confirmation"),
                                    working ? tr("A job is running. Stop the chamber and close the application, or close only the "
                                                 "application and let the job go on?")
                                            : tr("Turn the chamber off and close the application, or close only the application?"),
                                    QStringLiteral("question"), this);
    dialog->addChoice(tr("Cancel"), CANCEL);
    dialog->addChoice(tr("Exit app only"), APP_ONLY);
    dialog->addChoice(tr("Stop chamber and exit"), STOP_AND_EXIT, true);
    connect(dialog, &QDialog::finished, this, [this, resHolder, working](int result) {
      if (STOP_AND_EXIT == result) {
        if (working) {
          resHolder->tcpGate->sendTextCmd("stop");
        }
        resHolder->tcpGate->sendTextCmd("shutdown");
        emit prepareToCloseApp();
      } else if (APP_ONLY == result) {
        emit prepareToCloseApp();
      }
    });
    dialog->open();
  });
}
}
