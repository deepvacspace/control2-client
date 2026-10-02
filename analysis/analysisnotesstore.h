#ifndef ANALYSISNOTESSTORE_H
#define ANALYSISNOTESSTORE_H

#include <QColor>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

namespace deepvaccontrol {

/**
 * @brief Persists data analysis notes in a JSON file:
 * - annotations (labelled time ranges, absolute time so they show up in every run covering them);
 * - variable rules per run (acceptable range of a channel);
 * - selected channels per run.
 */
class AnalysisNotesStore : public QObject {
  Q_OBJECT
public:
  struct Annotation {
    QString id;
    double  start{0.0}; // epoch sec
    double  end{0.0};   // epoch sec
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

  explicit AnalysisNotesStore(QString const &filePath, QObject *parent = nullptr);

  [[nodiscard]] QVector<Annotation> annotations(double start, double end) const;
  void                              addAnnotation(Annotation annotation);
  void                              removeAnnotation(QString const &id);

  [[nodiscard]] QVector<Rule> rules(QString const &runKey) const;
  void                        addRule(QString const &runKey, Rule rule);
  void                        removeRule(QString const &runKey, QString const &id);

  [[nodiscard]] QStringList channels(QString const &runKey) const;
  void                      setChannels(QString const &runKey, QStringList const &channels);

signals:
  void annotationsChanged();
  void rulesChanged(QString const &runKey);

private:
  void load();
  void save() const;

  QString                       m_filePath;
  QVector<Annotation>           m_annotations;
  QHash<QString, QVector<Rule>> m_rules;
  QHash<QString, QStringList>   m_channels;
};

} // namespace deepvaccontrol

#endif // ANALYSISNOTESSTORE_H
