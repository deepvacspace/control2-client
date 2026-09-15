#ifndef PROGRAMTABLEDELEGATE_H
#define PROGRAMTABLEDELEGATE_H

#include <QStyledItemDelegate>

#include "programmodel.h"

namespace deepvaccontrol {
class ProgramTableDelegate : public QStyledItemDelegate {
  Q_OBJECT
public:
  explicit ProgramTableDelegate(ProgramModel *model, QObject *parent = nullptr);
  void textPaint(QPainter *painter, QRect r, QString const &text) const;

private:
  ProgramModel *m_model;
  QStringList   m_paramOrder;

  // QAbstractItemDelegate interface
public:
  void paint(QPainter *painter, QStyleOptionViewItem const &option, QModelIndex const &index) const override;
};
} // namespace deepvaccontrol

#endif // PROGRAMTABLEDELEGATE_H
