#ifndef DOUBLEVALIDATOR_H
#define DOUBLEVALIDATOR_H

#include <QValidator>

class DoubleValidator : public QDoubleValidator {
  Q_OBJECT
public:
  explicit DoubleValidator(QObject *parent = nullptr);
  DoubleValidator(double bottom, double top, int decimals, bool scientific, bool weakCkeck, QObject *parent);
  virtual State validate(QString &string, int &) const;

private:
  bool m_weakCkeck;

signals:
  void overMax();
};

#endif // DOUBLEVALIDATOR_H
