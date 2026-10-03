#ifndef EXPERIMENTREPORT_H
#define EXPERIMENTREPORT_H

#include <QImage>
#include <QPair>
#include <QString>
#include <QVector>

#include "analysisnotesstore.h"
#include "experimentanalysis.h"
#include "experimentlibrary.h"

namespace deepvaccontrol {

// Everything an experiment report consolidates.
struct ExperimentReportData {
  ExperimentRun                           run;
  core::ExperimentAnalysis const         *analysis{nullptr};
  QVector<ExperimentEvent>                events; // epoch times
  QVector<AnalysisNotesStore::Annotation> annotations;
  QVector<AnalysisNotesStore::Rule>       rules;
  QString                                 author;
  QVector<QPair<QString, QImage>>         charts;         // caption, image
  QString                                 comparisonHtml; // ComparisonPanel::html(), empty without a comparison
};

namespace experimentreport {
// embedImages: images as data: URLs (standalone .html); otherwise they are referenced as chartN.png
QString html(ExperimentReportData const &data, bool embedImages);
bool    writeHtml(QString const &path, ExperimentReportData const &data);
bool    writePdf(QString const &path, ExperimentReportData const &data);
} // namespace experimentreport

} // namespace deepvaccontrol

#endif // EXPERIMENTREPORT_H
