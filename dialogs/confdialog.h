#ifndef CONFDIALOG_H
#define CONFDIALOG_H

#include <QWidget>

#include "messagedialog.h"

namespace deepvaccontrol {
class ConfDialog : public MessageDialog {
  Q_OBJECT
public:
  explicit ConfDialog(QString const &title,
                      QString const &message,
                      QString const &btn1Name = tr("Yes"),
                      QString const &btn2Name = tr("No"),
                      QWidget       *parent   = nullptr);
  ~ConfDialog() {
  }
};
} // namespace deepvaccontrol

#endif // CONFDIALOG_H
