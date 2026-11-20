#ifndef PROGRAMSTEPPER_H
#define PROGRAMSTEPPER_H

#include <QPoint>
#include <QVector>
#include <QWidget>

class QFrame;
class QLabel;
class QPushButton;
class QScrollBar;
class QVBoxLayout;

namespace deepvaccontrol {

/**
 * @brief Program page, left: the steps of an experiment one after the other, as a cloud console's create wizard.
 *
 * Each step is a card with its number and title. The open one shows its content with Back and Next; the others show
 * their summary under the title. A step done (before the open one) is opened again with a tap on its title, unless
 * the steps are locked (a job runs: the last step stays open). Next does not open the next step by itself: the owner
 * checks the step (nextRequested) and opens the next one.
 */
class ProgramStepper : public QWidget {
  Q_OBJECT
public:
  explicit ProgramStepper(QWidget *parent = nullptr);

  int  addStep(QString const &title, QWidget *content); // its index
  void setHeaderWidget(int step, QWidget *widget);       // beside the title while the step is open ("Add")
  void setSummary(int step, QString const &text);        // under the title while the step is closed
  // Next of the step: off with why, shown beside it ("Name or purpose is required")
  void setNextEnabled(int step, bool enabled, QString const &why = {});
  void setCurrent(int step);
  [[nodiscard]] int current() const {
    return m_current;
  }
  void setLocked(bool locked); // no going back to another step

signals:
  void nextRequested(int step);
  void currentChanged(int step);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  struct Step {
    QFrame      *card{nullptr};
    QFrame      *head{nullptr};
    QLabel      *number{nullptr};
    QLabel      *title{nullptr};
    QLabel      *summary{nullptr};
    QWidget     *headerWidget{nullptr};
    QWidget     *body{nullptr};
    QPushButton *back{nullptr};
    QPushButton *next{nullptr};
    QLabel      *why{nullptr};
  };

  void refresh(); // what is shown of each step
  void restyle();

  QVector<Step> m_steps;
  QVBoxLayout  *m_lay{nullptr};
  QScrollBar   *m_scrollBar{nullptr}; // its lane is the gap beside the steps when it shows
  int           m_current{0};
  bool          m_locked{false};
  QPoint        m_pressAt; // a tap, not a scroll
};

}

#endif // PROGRAMSTEPPER_H
