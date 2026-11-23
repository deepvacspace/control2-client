#ifndef CHOICEDIALOG_H
#define CHOICEDIALOG_H

#include "messagedialog.h"

namespace deepvaccontrol {

// The app's message window with any number of answers; exec() returns the code of the one pressed (0: closed).
class ChoiceDialog : public MessageDialog {
  Q_OBJECT
public:
  // icon: "question", "warning", "info", "error"
  ChoiceDialog(QString const &title, QString const &message, QString const &icon, QWidget *parent = nullptr);
  // left to right; the primary one (last) is highlighted and the default
  QPushButton *addChoice(QString const &text, int code, bool primary = false);
};

}

#endif // CHOICEDIALOG_H
