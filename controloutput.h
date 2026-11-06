#ifndef CONTROLOUTPUT_H
#define CONTROLOUTPUT_H

#include <QFrame>
#include <limits>

class QLabel;

namespace deepvaccontrol {

struct ResHolder;

// Control output from -100 % (cooling, pumping) to +100 % (heating, venting), filled from the centre.
class ControlBar : public QWidget {
  Q_OBJECT
public:
  explicit ControlBar(QWidget *parent = nullptr);
  void setValue(double value); // NaN: none

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  double m_value{std::numeric_limits<double>::quiet_NaN()};
};

// A control output with its value and bar, following the chamber (a dash while there is none).
class ControlOutputGauge : public QFrame {
  Q_OBJECT
public:
  enum class Kind : quint8 {
    TEMPERATURE, // temp_u: cooling .. heating
    PRESSURE     // pres_u: pumping .. venting
  };
  ControlOutputGauge(ResHolder *resHolder, Kind kind, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *event) override;

private:
  void refresh();

  ResHolder  *m_resHolder;
  Kind        m_kind;
  QLabel     *m_value{nullptr};
  ControlBar *m_bar{nullptr};
};

}

#endif // CONTROLOUTPUT_H
