#ifndef ANALYSISSERVICES_H
#define ANALYSISSERVICES_H

#include <QObject>
#include <QStringList>
#include <functional>

#include "experimentlibrary.h"

namespace deepvaccontrol {

class AnalysisNotesStore;
struct ResHolder;

/**
 * @brief What the chart page, the data analysis page and the assistant share, independent of any page: the
 * experiment library (kept up to date when an experiment is saved or received from a paired computer), the notes
 * (annotations, variable rules), experiment reports, and a way to show an experiment to the user.
 */
class AnalysisServices : public QObject {
  Q_OBJECT
public:
  // show an experiment (channels by state name, time range in elapsed s, NaN = whole run, other runs compared with
  // it); returns an error text or empty
  using ShowHandler = std::function<QString(ExperimentRun const &run, QStringList const &channels, double from, double to,
                                            QVector<ExperimentRun> const &compare)>;

  explicit AnalysisServices(ResHolder *resHolder, QObject *parent = nullptr);

  [[nodiscard]] ExperimentLibrary  *library() const;
  [[nodiscard]] AnalysisNotesStore *notes() const;
  // scans the recorded experiments the first time it is needed
  void ensureScanned();

  // writes an experiment report (and the comparison with `compare`) into the export folder; channels: the chart of the
  // report (state names, empty: the channels shown by default); returns an error text or empty, path set on success
  QString writeReport(ExperimentRun const &run, QVector<ExperimentRun> const &compare, bool pdf, QString &path,
                      QStringList const &channels = {});

  // the page that shows experiments (the chart page) registers itself
  void    setShowHandler(ShowHandler handler);
  QString show(ExperimentRun const &run, QStringList const &channels, double from, double to, QVector<ExperimentRun> const &compare);

private:
  ResHolder          *m_resHolder;
  ExperimentLibrary  *m_library;
  AnalysisNotesStore *m_notes;
  ShowHandler         m_show;
};

inline ExperimentLibrary *AnalysisServices::library() const {
  return m_library;
}

inline AnalysisNotesStore *AnalysisServices::notes() const {
  return m_notes;
}

}

#endif // ANALYSISSERVICES_H
