#ifndef CONFDIALOG_H
#define CONFDIALOG_H

#include <QWidget>

#include "messagedialog.h"

namespace control2 {
class ConfDialog : public MessageDialog {
  Q_OBJECT
public:
  explicit ConfDialog(QString const &title,
                      QString const &message,
                      QString const &btn1Name = tr("Да"),
                      QString const &btn2Name = tr("Нет"),
                      QWidget       *parent   = nullptr);
  ~ConfDialog() {
  }
};
} // namespace control2

#endif // CONFDIALOG_H
