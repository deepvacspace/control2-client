#ifndef INTERVALCARDLIST_H
#define INTERVALCARDLIST_H

#include <QPoint>
#include <QVector>
#include <QWidget>
#include <optional>

#include "programmodel.h"

class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QStandardItem;
class QVBoxLayout;

namespace deepvaccontrol {

class LineEditNum;
class LineEditScreen;
class LineEditTime;

/**
 * @brief Program page, Intervals drawer: one card per interval ("Ramp to 60.0 °C", its duration and when it ends).
 *
 * A tap on a card selects the interval (the graph shows it too) and opens it: label, variable (temperature or
 * pressure), setpoint, mode, duration and ramp rate are edited in place; Duplicate, Then hold (a ramp's) and Delete act
 * on it (the first interval cannot be deleted). Only the selected card is open.
 *
 * Temperature modes: Full speed (then the duration is a hold), Linear ramp from the previous target over the duration
 * (the rate is calculated), and Custom ramp rate: a linear ramp at a given rate (the duration is calculated). Pressure
 * goes to its setpoint directly: the PLC ramps temperature only.
 */
class IntervalCardList : public QWidget {
  Q_OBJECT
public:
  explicit IntervalCardList(QWidget *parent = nullptr);

  void setModel(ProgramModel *model);
  void setLabels(QStringList const &labels); // one per interval
  void setEditable(bool editable);           // the user may change the program
  void setNotes(QStringList const &notes);   // per interval, after its duration ("reaches the target in ≈ 12 min")
  void setPressureLabel(QString const &label); // the chamber's pressure ("Pressure, mbar"): its unit for the setpoint
  // the chamber's temperature now (none: not known): where the first interval ramps from
  void setStartTemperature(std::optional<double> temperature);
  void prepareForStart(); // a custom ramp in the first interval: its time from the chamber's temperature now
  void showInterval(int interval); // selected, its card open (also when it was selected with its card closed)
  // the interval (a copy of the ramp before it) made a hold of that ramp's target: full speed, held for the time given
  void makeHold(int interval, int seconds);

signals:
  void labelEdited(int interval, QString const &label);
  void duplicateRequested(int interval);
  void holdRequested(int interval); // "Then hold": a hold of the ramp's target right after it
  void deleteRequested(int interval);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  struct Card {
    QFrame *frame{nullptr};
    QFrame *head{nullptr};
    QLabel *number{nullptr};
    QLabel *title{nullptr};
    QLabel *subtitle{nullptr};
    QLabel *chevron{nullptr};
  };

  void rebuild();      // the cards (the number of intervals changed)
  void refreshTexts(); // titles, durations, ends and the open card's fields
  void openCard(int interval);
  void closeEditor();
  void buildEditor(int interval);
  void fillEditor();
  void styleCards();
  [[nodiscard]] quint32 flagsOf(int interval) const;
  void                  setFlags(int interval, quint32 flags);
  [[nodiscard]] QStandardItem *flagsItem(int interval) const;
  void                  commit(); // the model changed here: the graph and the other views follow

  [[nodiscard]] int    rowOf(QString const &name) const; // -1: not a parameter of this chamber
  [[nodiscard]] double valueOf(int row, int interval) const;
  [[nodiscard]] int    variableRow(quint32 flags) const; // the temperature's or the pressure's row
  [[nodiscard]] bool   isPressure(quint32 flags) const;
  [[nodiscard]] static int modeOf(quint32 flags);
  [[nodiscard]] std::optional<double> rampChange(int interval) const; // °C from the previous target
  [[nodiscard]] std::optional<double> rampRate(int interval) const;   // °C/min
  bool                  applyCustomRates(); // true: a duration changed
  [[nodiscard]] QString pressureUnit() const;

  ProgramModel  *m_model{nullptr};
  QStringList    m_labels;
  QStringList    m_notes;
  QString        m_pressureLabel;
  std::optional<double> m_startTemp;
  bool           m_editable{true};
  QVBoxLayout   *m_lay{nullptr};
  QVector<Card>  m_cards;
  int            m_open{-1};
  bool           m_committing{false};
  QPoint         m_pressAt; // a tap, not a scroll
  // the open card's editor
  QWidget                *m_editor{nullptr};
  QVector<LineEditNum *>  m_valueEdits; // per parameter row (null: not a number)
  QComboBox              *m_variableCombo{nullptr};
  QLabel                 *m_setpointLabel{nullptr};
  LineEditTime           *m_durationEdit{nullptr};
  QLabel                 *m_durationLabel{nullptr};
  QComboBox              *m_modeCombo{nullptr};
  LineEditNum            *m_rateEdit{nullptr};
  LineEditScreen         *m_labelEdit{nullptr};
  QPushButton            *m_duplicateBtn{nullptr};
  QPushButton            *m_holdBtn{nullptr};
  QPushButton            *m_deleteBtn{nullptr};
};

}

#endif // INTERVALCARDLIST_H
