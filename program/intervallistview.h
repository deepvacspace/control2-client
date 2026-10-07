#ifndef INTERVALLISTVIEW_H
#define INTERVALLISTVIEW_H

#include <QTableWidget>

#include "programmodel.h"

namespace deepvaccontrol {

// Program intervals as rows (#, label, setpoints, duration, mode) on the same ProgramModel; selecting a row selects
// the interval in the model and the program graph.
class IntervalListView : public QTableWidget {
  Q_OBJECT
public:
  explicit IntervalListView(QWidget *parent = nullptr);

  void setModel(ProgramModel *model);
  void setLabels(QStringList const &labels); // interval labels, one per interval

protected:
  void changeEvent(QEvent *event) override;

private:
  void rebuild();
  void selectRow(int interval);

  ProgramModel *m_model{nullptr};
  QStringList   m_labels;
  bool          m_selecting{false};
};

} // namespace deepvaccontrol

#endif // INTERVALLISTVIEW_H
