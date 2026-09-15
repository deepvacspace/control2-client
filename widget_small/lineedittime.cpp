#include "lineedittime.h"

#include <QEvent>

#include "utils.h"

namespace deepvaccontrol {
LineEditTime::LineEditTime(quint64 timeInSecs, quint64 min, quint64 max, bool secEnable, QWidget *parent)
    : QLineEdit{parent}, m_sysValue{nullptr}, m_value{timeInSecs}, m_min{min}, m_max{max}, m_secEnable{secEnable} {
  {
    QFont font("Monospace");
    font.setBold(true);
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(20 * utils::ssf());
    setFont(font);
  }
  setStyleSheet("selection-background-color: #CCCCCC");

  hEdit = new LineEditNum(0, 999, 0, false, this);
  mEdit = new LineEditNum(0, 59, 0, false, this);
  hEdit->setVisible(false);
  mEdit->setVisible(false);
  setFixedWidth(105 * utils::ssf());
  adjustSize();

  hEdit->setFixedHeight(this->height());
  mEdit->setFixedHeight(this->height());

  if (m_secEnable) {
    sEdit = new LineEditNum(0, 59, 0, false, this);
    sEdit->setVisible(false);
    setFixedWidth(152 * utils::ssf());
    sEdit->setFixedHeight(this->height());
  }

  updateTextByValue(m_value);

  installEventFilter(this);

  connect(hEdit, &LineEditNum::userEditingFinished, this, &LineEditTime::handleUserAction);
  connect(mEdit, &LineEditNum::userEditingFinished, this, &LineEditTime::handleUserAction);
  connect(hEdit, &LineEditNum::textChanged, this, &LineEditTime::updateTextByFields);
  connect(mEdit, &LineEditNum::textChanged, this, &LineEditTime::updateTextByFields);
  if (m_secEnable) {
    connect(sEdit, &LineEditNum::userEditingFinished, this, &LineEditTime::handleUserAction);
    connect(sEdit, &LineEditNum::textChanged, this, &LineEditTime::updateTextByFields);
  }
  //    connect(this, &LineEditTime::virtualKboardClosed, this, [&]{ setTime(getSeconds()); });
}

LineEditTime::LineEditTime(bool secEnable, QWidget *parent) : LineEditTime(59, 10, 3599999, secEnable, parent) {
}

LineEditTime::LineEditTime(SystemValue *sysVal, bool secEnable, QWidget *parent)
    : LineEditTime(sysVal->getValue(), sysVal->getMin(), sysVal->getMax(), secEnable, parent) {
  m_sysValue = sysVal;
  updateTextByValue(m_sysValue->getValue());
  connect(m_sysValue, &SystemValue::valueChanged, this, [&](float sec) {
    updateTextByValue(sec);
  });
}

void LineEditTime::setTime(quint32 seconds) {
  long t = seconds;
  if (t < m_min) {
    t = m_min;
  }
  if (t > m_max) {
    t = m_max;
  }

  if (!m_sysValue) {
    if (m_value == t) {
      return;
    }
    m_value = t;
    updateTextByValue(m_value);
  } else {
    if (m_sysValue->getValue() == t) {
      return;
    }
    m_sysValue->setValue(t);
  }
}

long LineEditTime::getSeconds() {
  long ret{hEdit->text().toLong() * 3600 + mEdit->text().toLong() * 60};
  if (m_secEnable) {
    ret += sEdit->text().toLong();
  }

  return ret;
}

bool LineEditTime::eventFilter(QObject *object, QEvent *event) {
  if (event->type() == QEvent::MouseButtonRelease && this->isEnabled()) {
    LineEditNum *const field{selectField(cursorPosition())};
    if (field) {
      field->callVirtualKBoard();
    }
    return true; // lets the event continue to the edit
  } else {
    return QObject::eventFilter(object, event);
  }
}

void LineEditTime::handleUserAction() {
  setTime(getSeconds());
  emit userEdited();
}

LineEditNum *LineEditTime::selectField(int index) {
  LineEditNum *field{nullptr};
  int          selectedField{-1};

  if (index < 4) {
    selectedField = 0;
    field         = hEdit;
  } else if (index < 7) {
    selectedField = 1;
    field         = mEdit;
  } else if (index < 10) {
    selectedField = 2;
    field         = sEdit;
  }

  switch (selectedField) {
    case 0:
      this->setSelection(0, 3);
      break;
    case 1:
      this->setSelection(4, 2);
      break;
    case 2:
      this->setSelection(7, 2);
      break;
    default:
      this->deselect();
      break;
  }
  return field;
}

void LineEditTime::updateTextByFields() {
  if (m_secEnable) {
    setText(QString("%1").arg(hEdit->text().toInt(), 3, 10, QChar('0')) + ":"
            + QString("%1").arg(mEdit->text().toInt(), 2, 10, QChar('0')) + ":"
            + QString("%1").arg(sEdit->text().toInt(), 2, 10, QChar('0')));
  } else {
    setText(QString("%1").arg(hEdit->text().toInt(), 3, 10, QChar('0')) + ":"
            + QString("%1").arg(mEdit->text().toInt(), 2, 10, QChar('0')));
  }
}

void LineEditTime::updateTextByValue(long sec) {
  if (m_secEnable) {
    setText(QString("%1").arg((long)(floor(sec / 3600)), 3, 10, QChar('0')) + ":"
            + QString("%1").arg((long)(floor(sec % 3600 / 60)), 2, 10, QChar('0')) + ":"
            + QString("%1").arg((long)(sec % 60), 2, 10, QChar('0')));
  }

  else {
    setText(QString("%1").arg((long)(floor(sec / 3600)), 3, 10, QChar('0')) + ":"
            + QString("%1").arg((long)(floor(sec % 3600 / 60)), 2, 10, QChar('0')));
  }

  hEdit->setText(QString::number(floor(sec / 3600)));
  mEdit->setText(QString::number(floor(sec % 3600 / 60)));
  if (m_secEnable) {
    sEdit->setText(QString::number(sec % 60));
  }
}
} // namespace deepvaccontrol
