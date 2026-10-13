#ifndef TOUCHSUPPORT_H
#define TOUCHSUPPORT_H

class QAbstractScrollArea;
class QWidget;

namespace deepvaccontrol::touch {

// a finger dragging the list / table scrolls it (kinetic), as on a phone; the mouse is unchanged
void enableScrolling(QAbstractScrollArea *area);

// pressing and holding a finger opens target's context menu (its customContextMenuRequested, at the press position
// in target's coordinates); events: the widget the finger touches (an item view's viewport), target when null.
// releaseFirst: end the press for the widget first (e.g. a tab bar would think a tab is still being dragged)
void enableLongPress(QWidget *target, QWidget *events = nullptr, bool releaseFirst = false);

} // namespace deepvaccontrol::touch

#endif // TOUCHSUPPORT_H
