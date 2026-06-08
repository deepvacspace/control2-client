#include "dataviewerlegend.h"

#include "utils.h"
namespace control2 {
CustomLegendItem::CustomLegendItem(QCPLegend *parent, QString const &label, QPen const &iconPen)
    : QCPPlottableLegendItem(parent, nullptr)
    , m_label{label}
    , m_iconPen{iconPen}
    , m_value{0.0}
    , m_value_visibility{true}
    , m_value_paddding{static_cast<size_t>(40U * utils::ssf())}
    , m_label_paddding{static_cast<size_t>(10U * utils::ssf())} {
}

void CustomLegendItem::setPen(QPen const &pen) {
  m_iconPen = pen;
}

void CustomLegendItem::drawLegendIcon(QCPPainter *painter, QRectF const &rect) {
  // draw fill:
  if (mPlottable && (Qt::NoBrush != mPlottable->brush().style())) {
    // applyFillAntialiasingHint(painter);
    painter->fillRect(QRectF(rect.left(), rect.top() + rect.height() / 2.0, rect.width(), rect.height() / 3.0),
                      mPlottable->brush());
  }

  // draw line horizontaly centered:
  applyDefaultAntialiasingHint(painter);
  painter->setPen(m_iconPen);
  painter->drawLine(
    QLineF(rect.left(),
           rect.top() + rect.height() / 2.0,
           rect.right() + 5 * utils::ssf(),
           rect.top() + rect.height() / 2.0)); // +5 on x2 else last segment is missing from dashed/dotted pens
}

void CustomLegendItem::draw(QCPPainter *painter) {
  painter->setFont(getFont());
  painter->setPen(QPen(getTextColor()));
  QSize const iconSize{static_cast<int>(mParentLegend->iconSize().width() * utils::ssf()),
                       mParentLegend->iconSize().height()};
  QRect const iconRect{
    mRect.topLeft(), QSize{iconSize.width(), mRect.height()}
  };

  QRect const textRect{painter->fontMetrics().boundingRect(0, 0, 0, iconSize.height(), Qt::TextDontClip, m_label)};
  auto        textHeight = qMax(
    textRect.height(),
    iconSize.height()); // if text has smaller height than icon, center text vertically in icon height, else align tops

  // draw value
  if (m_value_visibility) {
    painter->drawText(mRect.x() + iconSize.width() + mParentLegend->iconTextPadding(),
                      mRect.y(),
                      m_value_paddding,
                      textHeight,
                      Qt::TextDontClip | Qt::AlignRight,
                      utils::formatScientific(m_value, 1)
                      // QString::number(m_value, 'f', 1)
                      );
  }

  size_t const totalTextPadding{m_value_visibility ? (m_value_paddding + m_label_paddding) : 0U};

  // draw label
  painter->drawText(mRect.x() + iconSize.width() + mParentLegend->iconTextPadding() + totalTextPadding,
                    mRect.y(),
                    textRect.width(),
                    textHeight,
                    Qt::TextDontClip,
                    m_label);

  // draw icon:
  painter->save();
  painter->setClipRect(iconRect, Qt::IntersectClip);
  drawLegendIcon(painter, iconRect);
  painter->restore();

  // draw icon border:
  if (getIconBorderPen().style() != Qt::NoPen) {
    painter->setPen(getIconBorderPen());
    painter->setBrush(Qt::NoBrush);
    auto const halfPen = qCeil(painter->pen().widthF() * 0.5) + 1;
    painter->setClipRect(mOuterRect.adjusted(
      -halfPen,
      -halfPen,
      halfPen,
      halfPen)); // extend default clip rect so thicker pens (especially during selection) are not clipped
    painter->drawRect(iconRect);
  }

  // // draw border
  // painter->setPen(QPen(QColor("ff0000")));
  // painter->drawRect(rect());
}

QSize CustomLegendItem::minimumOuterSizeHint() const {
  QSize result(0, 1);
  if (!visible()) {
    return result;
  }
  QFontMetrics const fontMetrics{getFont()};
  QSize const        iconSize{mParentLegend->iconSize()};
  QRect const        textRect{fontMetrics.boundingRect(0, 0, 0, iconSize.height(), Qt::TextDontClip, m_label)};
  size_t const       totalTextPadding{m_value_visibility ? (m_value_paddding + m_label_paddding) : 0U};

  result.setWidth(iconSize.width() + mParentLegend->iconTextPadding() + totalTextPadding + textRect.width());
  result.setHeight(qMax(textRect.height(), iconSize.height()));
  result.rwidth() += mMargins.left() + mMargins.right();
  result.rheight() += mMargins.top() + mMargins.bottom();
  return result;
}

DataViewerLegend::DataViewerLegend(QCustomPlot *plot) : m_plot{plot}, m_valueVisible{false} {
  if (m_plot) {
    m_legend = m_plot->legend;
    m_legend->setRowSpacing(-1);

    // add time label and time value element in layout
    m_timeLay = new QCPLayoutInset();
    m_timeLay->setMinimumSize(10 * utils::ssf(), 20 * utils::ssf());
    m_legend->addElement(0, 0, m_timeLay);

    m_timeValue = new QCPTextElement(m_plot, "00:00:00");
    m_timeLabel = new QCPTextElement(m_plot, QObject::tr("Время"));
    m_timeValue->setTextColor(Qt::black);
    m_timeValue->setTextFlags(Qt::AlignLeft);
    m_timeValue->setLayer("legend");
    m_timeLabel->setTextColor(Qt::black);
    m_timeLabel->setTextFlags(Qt::AlignLeft);
    m_timeLabel->setLayer("legend");

    m_timeLay->addElement(m_timeValue, Qt::AlignLeft | Qt::AlignVCenter);
    m_timeLay->addElement(m_timeLabel, Qt::AlignLeft | Qt::AlignVCenter);

    // left margin
    m_timeLabel->setAutoMargins(QCP::MarginSide::msBottom | QCP::MarginSide::msRight | QCP::MarginSide::msTop);
    updateTimePosition();
  }
}

void DataViewerLegend::setStyles(QVariantList const &stateListVariant) {
  auto brush = m_legend->brush();
  brush.setColor(QColor(255, 255, 255, 240));
  m_legend->setBrush(brush);

  quint16 const statesCount = static_cast<quint16>(stateListVariant.size());

  if (m_legendElements.size() != statesCount) { // reinit
    for (auto &oldElement : m_legendElements) {
      oldElement->deleteLater();
    }
    m_legendElements.clear();

    for (quint16 i{0U}; i < statesCount; ++i) {
      QVariantMap const stateMap{stateListVariant[i].toMap()};
      QString const     stateLabel{stateMap["label"].toString()};

      auto *legendElement = new CustomLegendItem(m_legend, stateLabel, QPen{});

      m_legend->addItem(legendElement);
      m_legendElements.push_back(legendElement);

      setStyle(i, stateMap);
    }
  } else { // just update
    for (quint16 i{0U}; i < statesCount; ++i) {
      QVariantMap const stateMap{stateListVariant[i].toMap()};
      setStyle(i, stateMap);
    }
  }
}

void DataViewerLegend::setStyle(int index, QVariantMap const &stateMapVariant) {
  if (index >= m_legendElements.size()) {
    return;
  }

  CustomLegendItem *legendElement = m_legendElements[index];

  quint32 const penWidth{stateMapVariant["width"].toUInt()};
  quint32 const penStyle{stateMapVariant["pen"].toUInt()};
  QColor const  color{QColor(stateMapVariant["color"].toString())};
  bool const    visible{stateMapVariant["visible"].toBool()};

  QPen iconPen(color);
  iconPen.setStyle(static_cast<Qt::PenStyle>(penStyle));
  iconPen.setWidth(penWidth);

  legendElement->setTextColor(color);
  legendElement->setVisible(visible);
  legendElement->setPen(iconPen);
}

void DataViewerLegend::setValues(QDateTime const &timeStamp, QVector<double> const &values) {
  // skip firest time element
  for (int i{1U}; i < values.size(); ++i) {
    CustomLegendItem *legendItem = qobject_cast<CustomLegendItem *>(m_legend->elementAt(i));
    if (legendItem) {
      legendItem->setValue(values[i - 1]);
    }
  }
  m_timeValue->setText(timeStamp.toString("hh:mm:ss"));
}

void DataViewerLegend::setVisible(bool value) {
  m_legend->setVisible(value);
  m_plot->layer("legend")->replot();
}

void DataViewerLegend::setValueVisible(bool visible) {
  m_valueVisible = visible;

  for (int i{0U}; i < m_legend->elementCount(); ++i) {
    CustomLegendItem *legendItem = qobject_cast<CustomLegendItem *>(m_legend->elementAt(i));
    if (legendItem) {
      legendItem->setValueVisible(visible);
    }
  }

  // time layout
  m_timeLay->setVisible(m_valueVisible);
  if (visible) {
    m_legend->addElement(0, 0, m_timeLay);
    updateTimePosition();
  } else {
    m_legend->take(m_timeLay);
  }

  m_plot->layer("legend")->replot();
}

void DataViewerLegend::updateTimePosition() {
  size_t const totalTextPadding{static_cast<size_t>(
    m_valueVisible ? (/*m_value_paddding*/ 40 * utils::ssf() + /*m_label_paddding*/ 10 * utils::ssf()) : 0U)};

  m_timeLabel->setMargins(
    QMargins(m_legend->iconSize().width() * utils::ssf() + m_legend->iconTextPadding() + totalTextPadding, 2, 0, 2));

  m_timeValue->setMargins(QMargins(36 * utils::ssf(), 2, 0, 2));
}
} // namespace control2
