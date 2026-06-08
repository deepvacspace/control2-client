#ifndef VIRTUALNUMKEYBOARD_H
#define VIRTUALNUMKEYBOARD_H

#include <QDialog>
#include <QPushButton>

namespace control2{
class LineEditNum;

class VirtualNumKeyboard : public QDialog {
  Q_OBJECT
public:
  explicit VirtualNumKeyboard(QWidget *parent = nullptr);
  LineEditNum *getLineEditNumOwner() const;
  void         setLineEditNum(LineEditNum *value);

private slots:
  void reject();
  void handleNumKeysPress();
  void handleKeyPress();

private:
  QList<QPushButton *> numericalButtons;
  QList<QPushButton *> controlButtons;
  QPushButton         *point;
  QPushButton         *minus;
  QPushButton         *e;
  QPushButton         *del;
  QPushButton         *enter;
  QPushButton         *cancel;
  LineEditNum         *host;
  bool                 notEdited;
  QString              prev;
  bool                 isScientific = false;
};
}

#endif // VIRTUALNUMKEYBOARD_H
