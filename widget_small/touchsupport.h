#ifndef TOUCHSUPPORT_H
#define TOUCHSUPPORT_H

class QAbstractScrollArea;
class QWidget;

namespace deepvaccontrol::touch {

// a finger dragging the list / table scrolls it (kinetic), as on a phone; the mouse is unchanged
void enableScrolling(QAbstractScrollArea *area);
// the same for every scroll area inside root, now and whenever one is added later (lists rebuilt, panels opened)
void enableScrollingWithin(QWidget *root);
// a finger that moved this far between press and release scrolled: not a tap
[[nodiscard]] int tapSlop();

// pressing and holding a finger opens target's context menu (its customContextMenuRequested, at the press position
// in target's coordinates); events: the widget the finger touches (an item view's viewport), target when null.
// releaseFirst: end the press for the widget first (e.g. a tab bar would think a tab is still being dragged)
void enableLongPress(QWidget *target, QWidget *events = nullptr, bool releaseFirst = false);

}

#endif // TOUCHSUPPORT_H
