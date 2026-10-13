#include "analysisnotesstore.h"

#include "annotationsdb.h"

namespace deepvaccontrol {

using storage::AnnotationRow;
using storage::AnnotationsDb;
using storage::RuleRow;

AnalysisNotesStore::AnalysisNotesStore(QString const &legacyJsonPath, QObject *parent) : QObject{parent} {
  AnnotationsDb::migrateJsonNotes(legacyJsonPath);
}

QVector<AnalysisNotesStore::Annotation> AnalysisNotesStore::annotations(double start, double end, QString const &runKey) const {
  QVector<Annotation> result;
  for (AnnotationRow const &row : AnnotationsDb::annotationsBetween(start, end, runKey)) {
    result.push_back({QString::number(row.id), row.runKey, row.t0 - row.x0, row.t0, row.t1, row.label, row.color, row.userName});
  }
  return result;
}

void AnalysisNotesStore::addAnnotation(Annotation const &annotation) {
  AnnotationRow row;
  row.runKey   = annotation.runKey;
  row.x0       = annotation.start - annotation.runStart;
  row.x1       = annotation.end - annotation.runStart;
  row.t0       = annotation.start;
  row.t1       = annotation.end;
  row.label    = annotation.label;
  row.color    = annotation.color;
  row.userName = annotation.user;
  AnnotationsDb::addAnnotation(row);
  emit annotationsChanged();
}

void AnalysisNotesStore::removeAnnotation(QString const &id) {
  AnnotationsDb::removeAnnotation(id.toLongLong());
  emit annotationsChanged();
}

void AnalysisNotesStore::updateAnnotation(QString const &id, Annotation const &annotation) {
  AnnotationsDb::removeAnnotation(id.toLongLong());
  addAnnotation(annotation); // emits annotationsChanged
}

QVector<AnalysisNotesStore::Rule> AnalysisNotesStore::rules(QString const &runKey) const {
  QVector<Rule> result;
  for (RuleRow const &row : AnnotationsDb::rules(runKey)) {
    result.push_back({QString::number(row.id), row.channel, row.lo, row.hi, row.name, row.color, row.userName});
  }
  return result;
}

void AnalysisNotesStore::addRule(QString const &runKey, Rule const &rule) {
  RuleRow row;
  row.runKey   = runKey;
  row.channel  = rule.channel;
  row.lo       = rule.lo;
  row.hi       = rule.hi;
  row.name     = rule.label;
  row.color    = rule.color;
  row.userName = rule.user;
  AnnotationsDb::addRule(row);
  emit rulesChanged(runKey);
}

void AnalysisNotesStore::removeRule(QString const &runKey, QString const &id) {
  AnnotationsDb::removeRule(id.toLongLong());
  emit rulesChanged(runKey);
}

QStringList AnalysisNotesStore::channels(QString const &runKey) const {
  return AnnotationsDb::channels(runKey);
}

void AnalysisNotesStore::setChannels(QString const &runKey, QStringList const &channels) {
  AnnotationsDb::setChannels(runKey, channels);
}

void AnalysisNotesStore::notifyExternalChange() {
  emit annotationsChanged();
  emit rulesChanged(QString{});
}

} // namespace deepvaccontrol
