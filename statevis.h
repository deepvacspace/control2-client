#ifndef STATEVIS_H
#define STATEVIS_H

#include <QWidget>

#include "systemvalue.h"

namespace control2 {
class StateVis : public QWidget {
  Q_OBJECT
public:
  explicit StateVis(
    QString const &name, SystemValue *x, SystemValue *r, SystemValue *u, double sf = 3, QWidget *parent = nullptr);
};
} // namespace control2

#endif // STATEVIS_H
