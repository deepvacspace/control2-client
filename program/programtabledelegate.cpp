#include "programtabledelegate.h"

#include <QPainter>

#include "utils.h"

namespace control2 {
ProgramTableDelegate::ProgramTableDelegate(ProgramModel *model, QObject *parent)
    : QStyledItemDelegate{parent}, m_model{model}, m_paramOrder{model->getParamListOrder()} {
}

void ProgramTableDelegate::textPaint(QPainter *painter, QRect r, QString const &text) const {
  painter->drawText(r.left(), r.top(), r.width(), r.height(), Qt::AlignLeft | Qt::AlignVCenter, text, &r);
}

void ProgramTableDelegate::paint(
  QPainter *painter, QStyleOptionViewItem const &option, QModelIndex const &index) const {
  QRect rect{option.rect.adjusted(2, 2, -2, -2)};

  int const     curIndex{index.column()};
  int const     paramIndex{index.row()};
  // QString const paramName{m_paramOrder.at(paramIndex)};
  quint32 const flags{
    m_model->getStandardModel()->item(m_paramOrder.indexOf("flags"), curIndex)->data(Qt::UserRole + 1).toUInt()};
  bool const showEnable{utils::getBitOfDword(flags, paramIndex)};

  if (!showEnable) {
    textPaint(painter, rect, QString());
    return;
  }

  // number=string
  if (index.data(Qt::DisplayRole).isValid()) {
    double const value{index.data(Qt::DisplayRole).toString().toDouble()};
    int const    prec{m_model->getParamListVariant().at(index.row()).toMap().value("prec").toInt()};
    bool const   isSci{m_model->getParamListVariant().at(index.row()).toMap().value("notation").toString()
                     == "scientific"};
    textPaint(painter, rect, isSci ? utils::formatScientific(value, prec) : QString::number(value, 'f', prec));
    return;
  }

  if (index.data(Qt::UserRole + 0).isValid()) // "duration"
  {
    textPaint(painter, rect, utils::timeFromSec(static_cast<long>(index.data(Qt::UserRole + 0).toDouble())));
    return;
  } else if (index.data(Qt::UserRole + 1).isValid()) // "flags"
  {
    textPaint(painter, rect, QString("%1").arg(index.data(Qt::UserRole + 1).toInt(), 8, 2, QLatin1Char('0')));
    return;
  }
  // else if (index.data(Qt::UserRole + 2).isValid()) // "lum"
  // {
  //   textPaint(painter, r, QString::number(utils::nLampToLux(index.data(Qt::UserRole + 2).toInt())));
  //   return;
  // }
  else if (index.data(Qt::UserRole + 3).isValid()) // "solar"
  {
    textPaint(painter, rect, tr("вкл."));
    return;
  }

  QStyledItemDelegate::paint(painter, option, index);
}
} // namespace control2
