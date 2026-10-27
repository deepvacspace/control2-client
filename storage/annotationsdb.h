#ifndef ANNOTATIONSDB_H
#define ANNOTATIONSDB_H

#include <QColor>
#include <QStringList>
#include <QVector>

namespace deepvaccontrol::storage {

// Row of Insight's "annotations" table plus the control-app absolute time range.
struct AnnotationRow {
  qint64  id{-1};
  QString runKey;
  QString userName;
  double  x0{0.0}; // elapsed sec from the run start (Insight)
  double  x1{0.0};
  double  t0{0.0}; // epoch sec (control app: shown on every run covering it)
  double  t1{0.0};
  QString label;
  QColor  color;
  QString createdAt;
  QString collabUid;
};

// Row of Insight's "variable_rules" table.
struct RuleRow {
  qint64  id{-1};
  QString runKey;
  QString userName;
  QString name; // label
  QString channel;
  double  lo{qQNaN()};
  double  hi{qQNaN()};
  QColor  color;
  QString createdAt;
  QString collabUid;
};

/**
 * @brief Annotations and variable rules in Deepvac Insight's format (deepvac_annotations.sqlite3),
 * plus a small table with the channels chosen per run. Stateless: safe from any thread.
 */
class AnnotationsDb {
public:
  // annotations overlapping [start, end]; those of experiments received from other computers
  // ("db:shared/..." run keys) only on that experiment itself
  static QVector<AnnotationRow> annotationsBetween(double start, double end, QString const &runKey);
  static QVector<AnnotationRow> allAnnotations();
  static qint64                 addAnnotation(AnnotationRow const &row); // returns the new id, -1 on error
  static void                   removeAnnotation(qint64 id);             // tombstones shared ones
  static void                   removeAnnotationWithoutTombstone(qint64 id);
  static void                   setAnnotationUid(qint64 id, QString const &uid);

  static QVector<RuleRow> rules(QString const &runKey);
  static QVector<RuleRow> allRules();
  static qint64           addRule(RuleRow const &row);
  static void             removeRule(qint64 id);
  static void             removeRuleWithoutTombstone(qint64 id);
  static void             setRuleUid(qint64 id, QString const &uid);

  static QStringList tombstones(QString const &kind); // "annotation" | "variable_rule"
  static void        clearTombstone(QString const &uid);

  static QStringList channels(QString const &runKey);
  static void        setChannels(QString const &runKey, QStringList const &channels);

  // one-time import of the earlier data/analysis/notes.json
  static void migrateJsonNotes(QString const &jsonPath);
};

}

#endif // ANNOTATIONSDB_H
