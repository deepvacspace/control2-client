#ifndef CHAMBERINDICATORS_H
#define CHAMBERINDICATORS_H

#include <QFrame>
#include <QHash>

class QLabel;
class QPushButton;

namespace deepvaccontrol {
class ChamberView;
struct ResHolder;

/**
 * @brief System > Diagram: beside the drawing, every component with its state now (a pill) and a Fitted / Not fitted
 * switch a Service Engineer sets for this chamber (saved in the settings, not sent to the PLC), with a warning where
 * the PLC reports otherwise.
 */
class ChamberIndicators : public QFrame {
  Q_OBJECT
public:
  ChamberIndicators(ResHolder *resHolder, ChamberView *view, QWidget *parent = nullptr);

private:
  struct Row {
    QFrame      *frame{nullptr};
    QLabel      *icon{nullptr};
    QLabel      *title{nullptr};
    QLabel      *note{nullptr}; // the PLC reports otherwise
    QLabel      *status{nullptr};
    QPushButton *fit{nullptr};
    QString      iconKind;
  };
  void refresh();

  ResHolder          *m_resHolder;
  ChamberView        *m_view;
  QHash<QString, Row> m_rows;
  QPushButton        *m_resetBtn{nullptr};
};

}

#endif // CHAMBERINDICATORS_H
