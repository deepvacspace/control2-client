#include "lineeditnum.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPointer>

#include "touchsupport.h"

#include "utils.h"
#include "virtualnumkeyboard.h"
#include "theme.h"

namespace deepvaccontrol {
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
  // one per tap, gone when it closes (not kept until the field goes). The field itself may go while it is open (its
  // page rebuilt): nothing of the field is touched after exec()
  QPointer<VirtualNumKeyboard> const keyboard = new VirtualNumKeyboard(this);
  m_keyboard                                  = keyboard;
  connect(keyboard, &VirtualNumKeyboard::accepted, this, &LineEditNum::afterCheck);
  connect(keyboard, &VirtualNumKeyboard::rejected, this, &LineEditNum::keyboradClosed);
  keyboard->exec();
  if (keyboard) {
    keyboard->deleteLater();
  }
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
  setFixedWidth(theme::dp(285));
  //    setStyleSheet("selection-background-color: #dce6ff");
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
  if (m_allowEmpty && text().trimmed().isEmpty()) {
    return;
  }
  double value{number()};

  if (value < m_validator->bottom()) {
    value = m_validator->bottom();
  }

  if (value > m_validator->top()) {
    value = m_validator->top();
  }

  setText(formatted(value));
}

QString LineEditNum::formatted(double value) const {
  return m_isScientific ? utils::formatSciE(value) : QString::number(value, 'f', m_validator->decimals());
}

double LineEditNum::number(bool *ok) const {
  return utils::parseNumber(text(), ok);
}

bool LineEditNum::eventFilter(QObject *object, QEvent *event) {
  // the keyboard opens on a tap: a finger that moved (scrolling the page) does not open it
  if (event->type() == QEvent::MouseButtonPress) {
    m_pressAt = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
  }
  if (event->type() == QEvent::MouseButtonRelease && this->isEnabled()
      && ((static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - m_pressAt).manhattanLength() <= touch::tapSlop())) {
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
  } else {
    setText(formatted(m_sysValue->getValue()));
  }
}

void LineEditNum::updateVarByText() {
  if (!m_sysValue) {
    return;
  }

  m_sysValue->setValue(number());
}

}
