#include "lineeditnum.h"

#include <QEvent>

#include "utils.h"
#include "virtualnumkeyboard.h"

namespace control2 {
LineEditNum::LineEditNum(double min, double max, int prec, bool weakCkeck, QWidget *parent)
    : QLineEdit(parent), m_sysValue(nullptr), m_weakCheck(weakCkeck) {
  installEventFilter(this); // LineEditNum::callVirtualKBoard()
  m_validator = new DoubleValidator(min, max, prec, false, m_weakCheck, this);
  m_validator->setLocale(QLocale("C"));
  setValidator(m_validator);
  updateText();
  setScreenInputStyle();
}

LineEditNum::LineEditNum(QWidget *parent) : LineEditNum(0, 1000, 1, true, parent) {
}

LineEditNum::LineEditNum(SystemValue *sysValue, QWidget *parent)
    : LineEditNum(sysValue->getMin(), sysValue->getMax(), sysValue->getPrec(), true, parent) {
  m_sysValue = sysValue;
  //    connect(this, &LineEditNum::virtualKboardClosed, &LineEditNum::updateVarByText);
  //    connect(this, &LineEditNum::virtualKboardClosed, this, &LineEditNum::userEditingFinished);
  connect(m_sysValue, &SystemValue::valueChanged, this, &LineEditNum::updateText);
  connect(m_sysValue, &SystemValue::minMaxChanged, this, [this] {
    setMinMaxPrec(m_sysValue->getMin(), m_sysValue->getMax(), getPrec());

    // check range
    checkValidator();
    updateVarByText();
  });

  if (m_sysValue->isSciNotation()) {
    setIsScientific(true);
  }

  updateText();
}

LineEditNum::~LineEditNum() {
}

void LineEditNum::callVirtualKBoard() {
  m_keyboard = new VirtualNumKeyboard(this);
  connect(m_keyboard, &VirtualNumKeyboard::accepted, this, &LineEditNum::afterCheck);
  connect(m_keyboard, &VirtualNumKeyboard::rejected, this, &LineEditNum::keyboradClosed);
  m_keyboard->exec();
}

void LineEditNum::setMinMaxPrec(double min, double max, int prec) {
  m_validator->setBottom(min);
  m_validator->setTop(max);
  m_validator->setDecimals(prec);
  checkValidator();
}

void LineEditNum::setScreenInputStyle() {
  QFont font("Monospace");
  font.setBold(true);
  font.setStyleHint(QFont::TypeWriter);
  font.setPointSize(20 * utils::ssf());
  setFont(font);
  setFixedWidth(152 * utils::ssf());
  //    setStyleSheet("selection-background-color: #CCCCCC");
}

void LineEditNum::setValue(float value) {
  setText(QString::number(value));
  checkValidator();
}

void LineEditNum::afterCheck() {
  checkValidator();

  //    emit virtualKboardClosed();
  updateVarByText();
  emit userEditingFinished();
}

void LineEditNum::checkValidator() {
  double value{text().toDouble()};

  if (value < m_validator->bottom()) {
    value = m_validator->bottom();
  }

  if (value > m_validator->top()) {
    value = m_validator->top();
  }

  if (!m_isScientific) {
    setText(QString::number(value, 'f', m_validator->decimals()));
  } else {
    setText(QString::number(value, 'E', m_validator->decimals()));
  }
}

bool LineEditNum::eventFilter(QObject *object, QEvent *event) {
  if (event->type() == QEvent::MouseButtonRelease && this->isEnabled()) {
    callVirtualKBoard();
    return true; // lets the event continue to the edit
  } else {
    return QObject::eventFilter(object, event);
  }
}

void LineEditNum::setIsScientific(bool value) {
  m_isScientific = value;
  if (m_isScientific) {
    m_validator->setNotation(QDoubleValidator::ScientificNotation);
  }
}

void LineEditNum::updateText() {
  if (!m_sysValue) {
    setText(QString::number(0, 'f', m_validator->decimals())); // FIXME 0 or minValue=m_validator->bottom()
  } else if (!m_isScientific) {
    setText(QString::number(m_sysValue->getValue(), 'f', m_validator->decimals()));
  } else {
    setText(QString::number(m_sysValue->getValue(), 'E', m_validator->decimals()));
  }
}

void LineEditNum::updateVarByText() {
  if (!m_sysValue) {
    return;
  }

  m_sysValue->setValue(text().toDouble());
}

} // namespace control2
