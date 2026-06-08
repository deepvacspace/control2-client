#ifndef FORCEOUTLISTWIDGET_H
#define FORCEOUTLISTWIDGET_H

#include <QWidget>

#include "forceoutsinglewidget.h"
#include "resholder.h"
#include "systemvalue.h"

namespace control2 {
class ForceOutListWidget : public QWidget {
  Q_OBJECT
public:
  explicit ForceOutListWidget(
    ResHolder *resHolder, SystemValue *outsSystVal, QVariantList const &forceOutVariantList, QWidget *parent = nullptr);

private:
  void         sendForceOutMsg(int count, QByteArray data);
  ResHolder   *m_resHolder;
  SystemValue *m_outsSystVal;

  QList<ForceOutSingleWidget *> m_outWidgets;
};
} // namespace control2

#endif // FORCEOUTLISTWIDGET_H
