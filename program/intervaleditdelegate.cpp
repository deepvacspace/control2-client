#include "intervaleditdelegate.h"

#include "lineeditnum.h"
#include "lineedittime.h"
// #include "lampslider.h"
#include <QCheckBox>
#include <QComboBox>

#include "utils.h"

namespace deepvaccontrol {
IntervalEditDelegate::IntervalEditDelegate(QObject *parent) : QItemDelegate(parent) {
}

void IntervalEditDelegate::setEditorData(QWidget *editor, QModelIndex const &index) const {
  LineEditTime *editTime = qobject_cast<LineEditTime *>(editor);
  LineEditNum  *editLine = qobject_cast<LineEditNum *>(editor);
  // LampSlider   *editLux  = qobject_cast<LampSlider *>(editor);

  if (editTime != nullptr) {
    editTime->setTime(index.data(Qt::UserRole + 0).toLongLong());
    return;
  } else if (editLine != nullptr) {
    editLine->setText(editLine->formatted(index.data(Qt::DisplayRole).toDouble()));
    return;
  } else {
    QItemDelegate::setEditorData(editor, index);
  }
}

void IntervalEditDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, QModelIndex const &index) const {
  LineEditTime *editTime = qobject_cast<LineEditTime *>(editor);
  // LampSlider   *editLum  = qobject_cast<LampSlider *>(editor);

  if (editTime != nullptr) {
    QVariant value((int)editTime->getSeconds());
    model->setData(index, value, Qt::UserRole + 0);
    return;
    // } else if (editLum != nullptr) {
    //   QVariant value((int)editLum->value());
    //   model->setData(index, value, Qt::UserRole + 2);
    //   return;
  } else if (auto *editLine = qobject_cast<LineEditNum *>(editor)) {
    model->setData(index, editLine->number(), Qt::EditRole); // the value, not its text ("1.0·E-02")
    return;
  } else {
    // do nothing, use orinar setModelData
  }
  QItemDelegate::setModelData(editor, model, index);
}
}
