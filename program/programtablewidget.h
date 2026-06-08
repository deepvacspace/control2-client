#ifndef PROGRAMTABLEWIDGET_H
#define PROGRAMTABLEWIDGET_H

#include <QFrame>
#include <QTableView>

#include "programmodel.h"

namespace control2 {
class ProgramTableWidget : public QFrame {
  Q_OBJECT
public:
  explicit ProgramTableWidget(QWidget *parent = nullptr);
  ~ProgramTableWidget() {
  }
  void setModel(ProgramModel *model);
  int  getSelectedIndex() const;
  void selectIndex(int index);

signals:
  void rowSelected(int);
  void doubleClicked(QModelIndex const &index);

private:
  ProgramModel *m_model;
  QTableView *m_tableView;
  void verticalResizeTableViewToContents();
};
} // namespace control2

#endif // PROGRAMTABLEWIDGET_H
