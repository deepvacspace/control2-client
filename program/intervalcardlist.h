#ifndef INTERVALCARDLIST_H
#define INTERVALCARDLIST_H

#include <QPoint>
#include <QVector>
#include <QWidget>

#include "programmodel.h"

class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace deepvaccontrol {

class LineEditNum;
class LineEditScreen;
class LineEditTime;

/**
 * @brief Program page, Intervals drawer: one card per interval ("Ramp to 60.0 °C", its duration and when it ends).
 *
 * A tap on a card selects the interval (the graph shows it too) and opens it: target, duration, mode and label are
 * edited in place; Duplicate and Delete act on it. Only the selected card is open.
 */
class IntervalCardList : public QWidget {
  Q_OBJECT
public:
  explicit IntervalCardList(QWidget *parent = nullptr);

  void setModel(ProgramModel *model);
  void setLabels(QStringList const &labels); // one per interval
  void setEditable(bool editable);           // the user may change the program
  void setNotes(QStringList const &notes);   // per interval, after its duration ("reaches the target in ≈ 12 min")

signals:
  void labelEdited(int interval, QString const &label);
  void duplicateRequested(int interval);
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
  void                  commit(); // the model changed here: the graph and the other views follow

  ProgramModel  *m_model{nullptr};
  QStringList    m_labels;
  QStringList    m_notes;
  bool           m_editable{true};
  QVBoxLayout   *m_lay{nullptr};
  QVector<Card>  m_cards;
  int            m_open{-1};
  bool           m_committing{false};
  QPoint         m_pressAt; // a tap, not a scroll
  // the open card's editor
  QWidget                *m_editor{nullptr};
  QVector<LineEditNum *>  m_valueEdits; // per parameter row (null: not a number)
  LineEditTime           *m_durationEdit{nullptr};
  QComboBox              *m_modeCombo{nullptr};
  LineEditScreen         *m_labelEdit{nullptr};
  QPushButton            *m_duplicateBtn{nullptr};
  QPushButton            *m_deleteBtn{nullptr};
};

}

#endif // INTERVALCARDLIST_H
