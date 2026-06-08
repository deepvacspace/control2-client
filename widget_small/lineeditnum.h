#ifndef LINEEDITNUM_H
#define LINEEDITNUM_H

#include <QLineEdit>

#include "doublevalidator.h"
#include "systemvalue.h"

namespace control2 {
class VirtualNumKeyboard;

class LineEditNum : public QLineEdit {
  Q_OBJECT
public:
  explicit LineEditNum(QWidget *parent = nullptr);
  explicit LineEditNum(double min, double max, int prec, bool weakCkeck = false, QWidget *parent = nullptr);
  explicit LineEditNum(SystemValue *sysValue, QWidget *parent = nullptr);
  virtual ~LineEditNum();
  void                   callVirtualKBoard();
  DoubleValidator const *getValidator() const;
  void                   setMinMaxPrec(double min, double max, int prec);
  void                   setScreenInputStyle();
  // Set value with validator checking
  void                   setValue(float value);
  int                    getPrec() const;
  int                    getMin() const;
  int                    getMax() const;
  void                   setPrec(int value);
  void                   setMin(int value);
  void                   setMax(int value);
  bool                   getIsScientific() const;
  void                   setIsScientific(bool value);

signals:
  // user make some changes
  void userEditingFinished();
  // user close dialog (reject changes)
  void keyboradClosed();

private:
  void                afterCheck();
  void                checkValidator();
  void                updateText();
  void                updateVarByText();
  DoubleValidator    *m_validator;
  VirtualNumKeyboard *m_keyboard;
  SystemValue        *m_sysValue;
  bool                m_weakCheck;
  bool                m_isScientific = false;

protected:
  bool eventFilter(QObject *object, QEvent *event);
};

inline DoubleValidator const *LineEditNum::getValidator() const {
  return m_validator;
}

inline int LineEditNum::getPrec() const {
  return m_validator->decimals();
}

inline int LineEditNum::getMin() const {
  return m_validator->bottom();
}

inline int LineEditNum::getMax() const {
  return m_validator->top();
}

inline void LineEditNum::setPrec(int value) {
  m_validator->setDecimals(value);
  checkValidator();
}

inline void LineEditNum::setMin(int value) {
  m_validator->setBottom(value);
  checkValidator();
}

inline void LineEditNum::setMax(int value) {
  m_validator->setTop(value);
  checkValidator();
}

inline bool LineEditNum::getIsScientific() const {
  return m_isScientific;
}

} // namespace control2

#endif // LINEEDITNUM_H
