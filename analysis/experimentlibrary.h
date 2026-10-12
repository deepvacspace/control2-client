#ifndef EXPERIMENTLIBRARY_H
#define EXPERIMENTLIBRARY_H

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <functional>
#include <memory>
#include <optional>

namespace deepvaccontrol {

/**
 * @brief One recorded experiment: a continuous stretch of samples found in the
 * interval files (data/yyyy-MM-dd_HH-mm-ss.dat). It may span several day files.
 */
struct ExperimentRun {
  QString     key;   // stable id used for tabs and saved notes
  QString     title; // human readable
  double      start{0.0}; // epoch sec of the first sample
  double      end{0.0};   // epoch sec of the last sample
  qint64      samples{0};
  QStringList files; // interval files the run is read from
  bool        custom{false}; // user defined time range instead of a detected experiment
  QString     storeKey;      // run saved in the experiment database (storage::RunStore)
  QString     chamber;       // database runs: chamber it was recorded on
  QString     details;       // database runs: DUT, operator, purpose, tags, job (searchable)

  [[nodiscard]] double duration() const {
    return end - start;
  }
};

struct ExperimentData {
  ExperimentRun            run;
  QVector<double>          times;  // epoch sec
  QVector<QVector<double>> states; // [channel][sample]
  QStringList              channelNames; // database runs: state name of each channel (else settings order)
  QJsonObject              manifest;     // database runs: core::RunManifest JSON (configuration, events, ...), empty otherwise
};

struct ExperimentEvent {
  double  time{0.0}; // epoch sec
  int     type{0};   // ActionManager::ActionType
  QString msg;
};

struct ChannelInfo {
  QString name;
  QString label;
  QColor  color;
  Qt::PenStyle penStyle{Qt::SolidLine}; // as on the main chart ("pen" in the settings)
  bool    defaultVisible{false};
  bool    scientific{false}; // drawn on a logarithmic axis
};

/**
 * @brief Finds previous experiments in the interval files and loads their data.
 * Scanning is done in a background thread, loading is synchronous with a small cache.
 */
class ExperimentLibrary : public QObject {
  Q_OBJECT
public:
  // Per file summary of possible split points, independent of the gap threshold.
  struct FileBreak {
    double tBefore{0.0};   // time of the last valid sample before the break
    double tAfter{0.0};    // time of the first valid sample after the break
    qint64 validBefore{0}; // number of valid samples before the break
    bool   forced{false};  // NaN terminator (application stop) between samples
  };
  struct FileSummary {
    QString            path;
    qint64             size{0};
    qint64             modified{0};
    double             first{0.0};
    double             last{0.0};
    qint64             valid{0};
    bool               endsWithTerminator{false};
    QVector<FileBreak> breaks;
  };

  // statesProvider returns the current "states" settings (labels, colors) used to describe channels
  explicit ExperimentLibrary(QString const               &intervalsDir,
                             QString const               &actionsFile,
                             std::function<QVariantList()> statesProvider,
                             QObject                     *parent = nullptr);
  ~ExperimentLibrary() override;

  void refresh();
  void setGapThresholdSec(double sec);

  [[nodiscard]] bool                          isScanning() const;
  // refresh() was called at least once
  [[nodiscard]] bool                          hasScanned() const;
  [[nodiscard]] double                        gapThresholdSec() const;
  [[nodiscard]] QVector<ExperimentRun> const &runs() const;
  // runs saved in the experiment database (recorded here or received from other computers)
  [[nodiscard]] QVector<ExperimentRun> const &storedRuns() const;
  // every experiment (recorded days and database), oldest first: experiment #N is element N - 1
  [[nodiscard]] QVector<ExperimentRun> chronological() const;
  [[nodiscard]] std::optional<ExperimentRun>  findRun(QString const &key) const;
  [[nodiscard]] ExperimentRun                 makeRangeRun(double start, double end) const;

  std::shared_ptr<ExperimentData const> load(ExperimentRun const &run, QString *error = nullptr);
  QVector<ExperimentEvent>              events(double start, double end);
  QVector<ChannelInfo>                  channels(ExperimentData const &data) const;

  static bool readIntervalFile(QString const &path, QVector<double> &times, QVector<QVector<double>> &states);
  static QString formatDuration(double sec);

signals:
  void scanStarted();
  void runsChanged();

private:
  struct ParsedFile {
    QString                  path;
    qint64                   modified{0};
    QVector<double>          times;
    QVector<QVector<double>> states;
  };

  void                                    applySegmentation();
  std::shared_ptr<ParsedFile const>       parsedFile(QString const &path);
  QVector<ExperimentRun>                  segment(QVector<FileSummary> const &files) const;

  QString      m_intervalsDir;
  QString      m_actionsFile;
  std::function<QVariantList()> m_statesProvider;
  double       m_gapThresholdSec{300.0};

  bool                         m_scanning{false};
  bool                         m_scanned{false};
  bool                         m_rescanRequested{false};
  QHash<QString, FileSummary>  m_summaryCache;
  QVector<FileSummary>         m_files;
  QVector<ExperimentRun>       m_runs;
  QVector<ExperimentRun>       m_storedRuns;

  QVector<std::shared_ptr<ParsedFile const>> m_parsedCache; // most recent first
  QVector<ExperimentEvent>                   m_events;
  qint64                                     m_eventsModified{-1};
};

inline bool ExperimentLibrary::isScanning() const {
  return m_scanning;
}

inline bool ExperimentLibrary::hasScanned() const {
  return m_scanned;
}

inline double ExperimentLibrary::gapThresholdSec() const {
  return m_gapThresholdSec;
}

inline QVector<ExperimentRun> const &ExperimentLibrary::runs() const {
  return m_runs;
}

inline QVector<ExperimentRun> const &ExperimentLibrary::storedRuns() const {
  return m_storedRuns;
}

} // namespace deepvaccontrol

#endif // EXPERIMENTLIBRARY_H
