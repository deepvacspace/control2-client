#ifndef MANUALGRAPH_H
#define MANUALGRAPH_H

#include <QColor>
#include <QFrame>
#include <optional>

class QCustomPlot;
class QCPGraph;

namespace deepvaccontrol {

// Control ▸ Manual: what the setpoints do from now. Each variable switched on: flat at the chamber's value, then in the
// middle of the plot a straight line to its setpoint (at its ramp rate, or at once at full speed), then flat to the end
// (held until stopped). A variable switched off: no line, no axis. The pressure on a logarithmic axis at the right, in
// the chamber's unit.
class ManualGraph : public QFrame {
  Q_OBJECT
public:
  struct Line {
    bool                  on{false};
    std::optional<double> now;    // the chamber's value; none: not known (the setpoint alone is drawn)
    double                target{0.0};
    std::optional<double> rate;   // per minute; none: full speed
  };

  explicit ManualGraph(QWidget *parent = nullptr);

  void setPressureUnit(QString const &unit);
  void setColors(QColor const &temperature, QColor const &pressure);
  void setLines(Line const &temperature, Line const &pressure);

protected:
  void showEvent(QShowEvent *event) override; // drawn when lines changed while hidden

private:
  void redraw();

  QCustomPlot *m_plot{nullptr};
  QCPGraph    *m_tempGraph{nullptr};
  QCPGraph    *m_presGraph{nullptr};
  Line         m_temp;
  Line         m_pres;
  bool         m_drawn{false};
  bool         m_stale{false}; // lines changed while hidden
};
}

#endif // MANUALGRAPH_H
