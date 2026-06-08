#include "doublevalidator.h"

DoubleValidator::DoubleValidator(QObject *parent) : QDoubleValidator(parent) {
}

DoubleValidator::DoubleValidator(
  double bottom, double top, int decimals, bool scientific, bool weakCkeck, QObject *parent)
    : QDoubleValidator(bottom, top, decimals, parent), m_weakCkeck(weakCkeck) {
  if (scientific) {
    setNotation(QDoubleValidator::ScientificNotation);
  }
}

QValidator::State DoubleValidator::validate(QString &string, int &) const {
  if (string.startsWith("00") || string.startsWith("-00")) {
    return Invalid;
  }

  //    if(string.contains('-'))
  //        if(string.indexOf(QChar('-')) != 0)
  //            return Invalid;

  if (string.length() > 1) {
    if (string.startsWith("0") && (string.at(1) != '.')) {
      return Invalid;
    }
  }

  if (string.length() > 2) {
    if (string.startsWith("-0") && (string.at(2) != '.')) {
      return Invalid;
    }
  }

  if (string.count('E') > 1) {
    return Invalid;
  }

  if (string.contains("E.") || string.contains(".E") || string.contains("E0") || string.contains("-E")) {
    return Invalid;
  }

  // check length of decimal places
  QChar const point{'.'};
  auto const  indexOfPoint = string.indexOf(point);
  if (-1 != indexOfPoint) {
    int const decimalsEnd
      = string.contains('E', Qt::CaseInsensitive) ? string.indexOf('e', 0, Qt::CaseInsensitive) : string.length();
    int lengthDecimals = (decimalsEnd - indexOfPoint) - 1;
    if (lengthDecimals > decimals()) {
      return Invalid;
    }
  }

  if (string.count(".") > 1) {
    return Invalid;
  }

  //    if(string.count("-")>1)
  //        return Invalid;

  if (!m_weakCkeck) {
    if (string.toDouble() < bottom()) {
      string = QString::number(bottom());
    }

    if (top() < string.toDouble()) {
      string = QString::number(top());
    }
  }

  return Acceptable;
}
