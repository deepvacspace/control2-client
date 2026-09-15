#ifndef INTERVALEDITDELEGATE_H
#define INTERVALEDITDELEGATE_H

#include <QItemDelegate>

namespace deepvaccontrol{
class IntervalEditDelegate : public QItemDelegate {
public:
  explicit IntervalEditDelegate(QObject *parent = nullptr);

  // QAbstractItemDelegate interface
public:
  void setEditorData(QWidget *editor, QModelIndex const &index) const override;
  void setModelData(QWidget *editor, QAbstractItemModel *model, QModelIndex const &index) const override;
};
}

#endif // INTERVALEDITDELEGATE_H
