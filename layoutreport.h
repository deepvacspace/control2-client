#ifndef LAYOUTREPORT_H
#define LAYOUTREPORT_H

#include <QString>

class QWidget;

namespace deepvaccontrol::layoutreport {

// What of a window (or dialog) as it is shown now does not fit, and the sizes of its fonts, into path as text:
// labels, buttons, check boxes, tabs and table headings whose text is wider (or taller) than the room they have,
// widgets reaching outside their window, and how many texts are drawn in each font size (with examples). Written
// beside each screen of --export-ui when the environment has DEEPVAC_EXPORT_LAYOUT=1
void write(QWidget const *window, QString const &path);

}

#endif // LAYOUTREPORT_H
