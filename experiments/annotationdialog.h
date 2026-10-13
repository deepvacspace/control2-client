#ifndef ANNOTATIONDIALOG_H
#define ANNOTATIONDIALOG_H

#include <QColor>
#include <QDateTime>
#include <QString>

class QComboBox;
class QWidget;

namespace deepvaccontrol::annotationdialog {

// the colours offered for annotations and variable rules (item data: "#rrggbb")
QComboBox *makeColorCombo(QWidget *parent, int height);

// asks the label and colour of a new annotation of the segment described by segmentText; false when cancelled.
// label stays empty when none is entered.
bool ask(QWidget *parent, QString const &segmentText, QString &label, QColor &color);

// changes an annotation: its label, colour and segment (clock times); false when cancelled
bool edit(QWidget *parent, QString &label, QColor &color, QDateTime &from, QDateTime &to);

} // namespace deepvaccontrol::annotationdialog

#endif // ANNOTATIONDIALOG_H
