#ifndef ANALYSISNOTESSTORE_H
#define ANALYSISNOTESSTORE_H

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QVector>

namespace deepvaccontrol {

/**
 * @brief Data analysis notes, stored in Deepvac Insight's annotations database
 * (data/deepvac_annotations.sqlite3, see storage::AnnotationsDb):
 * - annotations: labelled time ranges, shown in every run covering them;
 * - variable rules per run (acceptable range of a channel);
 * - selected channels per run.
 */
class AnalysisNotesStore : public QObject {
  Q_OBJECT
public:
  struct Annotation {
    QString id;
    QString runKey;          // run the annotation was drawn on
    double  runStart{0.0};   // that run's start, epoch sec
    double  start{0.0};      // epoch sec
    double  end{0.0};        // epoch sec
    QString label;
    QColor  color;
    QString user;
  };

  struct Rule {
    QString id;
    QString channel; // state name
    double  lo{qQNaN()};
    double  hi{qQNaN()};
    QString label;
    QColor  color;
    QString user;
  };

  // legacyJsonPath: notes.json written by earlier versions, imported once
  explicit AnalysisNotesStore(QString const &legacyJsonPath, QObject *parent = nullptr);

  [[nodiscard]] QVector<Annotation> annotations(double start, double end, QString const &runKey) const;
  void                              addAnnotation(Annotation const &annotation);
  void                              removeAnnotation(QString const &id);

  [[nodiscard]] QVector<Rule> rules(QString const &runKey) const;
  void                        addRule(QString const &runKey, Rule const &rule);
  void                        removeRule(QString const &runKey, QString const &id);

  [[nodiscard]] QStringList channels(QString const &runKey) const;
  void                      setChannels(QString const &runKey, QStringList const &channels);

  // the database was changed elsewhere (e.g. synced from another computer)
  void notifyExternalChange();

signals:
  void annotationsChanged();
  void rulesChanged(QString const &runKey);
};

} // namespace deepvaccontrol

#endif // ANALYSISNOTESSTORE_H
