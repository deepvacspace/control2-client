#ifndef COMPARISONDIALOG_H
#define COMPARISONDIALOG_H

#include <QDialog>

#include "chartcomparison.h"

class QComboBox;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace deepvaccontrol {
struct ResHolder;

/**
 * @brief Chooses what a comparison tab shows: experiments among the open tabs and from the experiment list, the
 * channels to compare and the alignment.
 */
class ComparisonDialog : public QDialog {
  Q_OBJECT
public:
  // a tab open on the chart page that can take part
  struct OpenTab {
    QString       key;
    QString       label;
    QString       name;
    ExperimentRun run;
  };
  static constexpr int MAX_EXPERIMENTS{8};

  // initial: an existing comparison to change (empty members: a new one, with `checkedTab` ticked)
  ComparisonDialog(ResHolder *resHolder, ExperimentLibrary *library, QVector<OpenTab> const &openTabs, ChartComparison const &initial,
                   QString const &checkedTab, QWidget *parent = nullptr);

  [[nodiscard]] ChartComparison comparison() const;

private:
  void addFromList(ExperimentRun const &run);
  void updateState();

  QVector<OpenTab> m_openTabs;
  QListWidget     *m_tabsList{nullptr};    // open tabs, ticked to take part
  QListWidget     *m_addedList{nullptr};   // experiments added from the list
  QListWidget     *m_channelsList{nullptr};
  QComboBox       *m_alignCombo{nullptr};
  QSpinBox        *m_alignSpin{nullptr};
  QPushButton     *m_okBtn{nullptr};
  QVector<ExperimentRun> m_added;
};

} // namespace deepvaccontrol

#endif // COMPARISONDIALOG_H
