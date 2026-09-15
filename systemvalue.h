#ifndef SYSTEMVALUE_H
#define SYSTEMVALUE_H

#include <QObject>
#include <QVariant>

namespace deepvaccontrol {
/**
 * @brief Object class for storing a value that can be synchronously used in all elements of the system.
 * Could be:
 *  - observed equipment state;
 *  - any kind of system configuration parameter.
 */
class SystemValue : public QObject {
  Q_OBJECT
public:
  SystemValue(QString const &name, float float_value = 67, QObject *parent = nullptr);
  SystemValue(QVariantMap const &variantMap, QObject *parent = nullptr);
  ~SystemValue() {
  }

  /**
   * @brief setValue
   * @param newValue
   * @param shadow set value WITHOUT NOTIFICATION
   */
  void setValue(float const newValue, bool shadow = false) noexcept;

  void notify();

  void setInvalid() noexcept;

  bool isValid() const noexcept;

  bool isSciNotation() const noexcept;

  void setBitOfFloat(quint8 position, bool bit_value, bool shadow = false);
  bool getBitOfFloat(quint8 position) const;
  void setBitOfUInt(quint8 position, bool bit_value, bool shadow = false);
  bool getBitOfUInt(quint8 position) const;

  float getValue() const noexcept {
    return m_value;
  }

  QString getName() const noexcept {
    return m_name;
  }

  void setLabel(QString const &newLabel) {
    m_label = newLabel;
  }

  QString getLabel() const noexcept {
    return m_label;
  }

  void setMin(double value) noexcept {
    m_min = value;
    emit minMaxChanged();
  }

  float getMin() const noexcept {
    return m_min;
  }

  void setMax(double value) noexcept {
    m_max = value;
    emit minMaxChanged();
  }

  float getMax() const noexcept {
    return m_max;
  }

  void setPrec(quint32 value) noexcept {
    m_prec = value;
  }

  int getPrec() const noexcept {
    return m_prec;
  }

  void setUserData(quint32 value) noexcept {
    m_userData = value;
  }

  quint32 getUserData() const noexcept {
    return m_userData;
  }

  void setSciNotation(bool value) noexcept {
    m_isSciNotation = value;
  }

  QVariantMap toVariant() const;

signals:
  void valueChanged(float);
  void minMaxChanged();

private:
  QString m_name;
  QString m_label;
  float   m_value;
  float   m_min;
  float   m_max;
  quint32 m_prec;
  quint32 m_userData;
  bool    m_isSciNotation;
};
} // namespace deepvaccontrol

#endif // SYSTEMVALUE_H
