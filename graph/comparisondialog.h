#ifndef COMPARISONDIALOG_H
#define COMPARISONDIALOG_H

#include <QDialog>

#include "chartcomparison.h"

class QLabel;
class QListWidget;
class QPushButton;

namespace deepvaccontrol {
struct ResHolder;

/**
 * @brief Chooses what a comparison tab shows: experiments among the open tabs and from the experiment list, the
 * channels to compare. The experiments are aligned on their starts.
 */
class ComparisonDialog : public QDialog {
  Q_OBJECT
public:
  // a tab open on the Analysis page that can take part
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
  QLabel          *m_experimentsBadge{nullptr}; // "2 of 8"
  QLabel          *m_channelsBadge{nullptr};
  QLabel          *m_hint{nullptr};             // what is missing, or ready
  QPushButton     *m_okBtn{nullptr};
  QVector<ExperimentRun> m_added;
};

}

#endif // COMPARISONDIALOG_H
