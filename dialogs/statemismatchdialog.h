#ifndef STATEMISMATCHDIALOG_H
#define STATEMISMATCHDIALOG_H

#include <QDialog>
#include <QStringList>

namespace deepvaccontrol {

// The connected chamber reports another list of values than settings.json describes: what differs and what the
// operator can do (finished() result: Choice).
class StateMismatchDialog : public QDialog {
  Q_OBJECT
public:
  enum Choice {
    STAY  = 0, // keep the chamber connected but unused
    BACK  = 2, // reconnect the last chamber that matched
    ADOPT = 3  // take the chamber's list into settings.json and restart
  };

  // backAddress empty: no chamber to go back to; missing: values the app needs that the chamber lacks (adopting
  // is then not offered)
  StateMismatchDialog(QString const     &address,
                      QStringList const &chamberNames,
                      QStringList const &appNames,
                      QString const     &backAddress,
                      QStringList const &missing,
                      QWidget           *parent = nullptr);

protected:
  void reject() override; // Escape = stay
};

}

#endif // STATEMISMATCHDIALOG_H
