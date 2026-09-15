#ifndef LINEEDITTIME_H
#define LINEEDITTIME_H
#include <QIntValidator>
#include <QLineEdit>
#include <QWidget>

#include "lineeditnum.h"
#include "systemvalue.h"
#include "virtualnumkeyboard.h"

namespace deepvaccontrol {

class MyIntValidator : public QIntValidator {
public:
  MyIntValidator(QObject *parent = 0) : QIntValidator(parent) {
  }
  MyIntValidator(int bottom, int top, QObject *parent) : QIntValidator(bottom, top, parent) {
  }

  QValidator::State validate(QString &s, int &) const {
    if (s.isEmpty() || (s.startsWith("-") && s.length() == 1)) {
      // allow empty field or standalone minus sign
      return QValidator::Intermediate;
    }
    // check range of value
    bool isNumber;
    int  value = locale().toInt(s, &isNumber);
    if (isNumber && bottom() <= value && value <= top()) {
      return QValidator::Acceptable;
    }
    return QValidator::Invalid;
  }
};

class LineEditTime : public QLineEdit {
  Q_OBJECT
public:
  LineEditTime(quint64 timeInSecs, quint64 min, quint64 max, bool secEnable = true, QWidget *parent = nullptr);
  LineEditTime(bool secEnable = true, QWidget *parent = nullptr);
  LineEditTime(SystemValue *sysValue, bool secEnable = true, QWidget *parent = nullptr);
  void setTime(quint32 seconds);
  long getSeconds();

  LineEditNum *hEdit;
  LineEditNum *mEdit;
  LineEditNum *sEdit;

signals:
  void virtualKboardClosed();
  void userEdited();

private slots:
  void updateTextByValue(long sec);

private:
  bool         eventFilter(QObject *object, QEvent *event);
  void         updateTextByFields();
  void         handleUserAction();
  LineEditNum *selectField(int index);

  SystemValue *m_sysValue;
  quint64      m_value;
  quint64      m_min;
  quint64      m_max;
  bool         m_secEnable;
};
} // namespace deepvaccontrol

#endif // LINEEDITTIME_H
