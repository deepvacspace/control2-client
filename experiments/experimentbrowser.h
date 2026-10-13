#ifndef EXPERIMENTBROWSER_H
#define EXPERIMENTBROWSER_H

#include <QWidget>

#include "experimentlibrary.h"

class QHBoxLayout;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace deepvaccontrol {

/**
 * @brief List of the experiments found by an ExperimentLibrary (saved in the database, recorded days) with a filter,
 * a refresh button and an arbitrary date / time range. Shared by the chart and the data analysis pages.
 */
class ExperimentBrowser : public QWidget {
  Q_OBJECT
public:
  // canSplit: the context menu also offers "Split right"
  explicit ExperimentBrowser(ExperimentLibrary *library, bool canSplit, QWidget *parent = nullptr);

  // an extra button at the end of the bottom row
  void addButton(QPushButton *button);

signals:
  void openRequested(ExperimentRun const &run, bool splitRight);

protected:
  void showEvent(QShowEvent *event) override;

private:
  void  populateTree();
  void  applyFilter();
  void  openItem(QTreeWidgetItem *item);
  void  openCustomRange();
  QDate selectedDay() const;

  ExperimentLibrary *m_library;
  bool               m_canSplit;
  QTreeWidget       *m_tree{nullptr};
  QLineEdit         *m_filterEdit{nullptr};
  QPushButton       *m_refreshBtn{nullptr};
  QHBoxLayout       *m_buttonsLay{nullptr};
};

} // namespace deepvaccontrol

#endif // EXPERIMENTBROWSER_H
