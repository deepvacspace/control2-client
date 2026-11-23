#ifndef DATAVIEWERLEGEND_H
#define DATAVIEWERLEGEND_H

#include "qcustomplot.h"

namespace deepvaccontrol {
/**
 * @brief Custom element for legend with value textbox
 */
class CustomLegendItem : public QCPPlottableLegendItem {
  Q_OBJECT
public:
  CustomLegendItem(QCPLegend *parent, QString const &label, QPen const &iconPen);
  // the line's value at the cursor, after its name ("" while there is none)
  void setValueText(QString const &text);
  // room for the value after the name (a fixed width: the box does not move while the values change)
  void setValueVisible(bool value);
  void setPen(QPen const &pen);

protected:
  void drawLegendIcon(QCPPainter *painter, QRectF const &rect);

  // property members:
  QString m_label;
  QString m_valueText;
  bool    m_value_visibility;
  QPen    m_iconPen;

  [[nodiscard]] int valueWidth(QFontMetrics const &metrics) const;

  // reimplemented virtual methods:
  virtual void  draw(QCPPainter *painter) override;
  virtual QSize minimumOuterSizeHint() const override;
};

inline void CustomLegendItem::setValueText(QString const &text) {
  m_valueText = text;
}

inline void CustomLegendItem::setValueVisible(bool visible) {
  m_value_visibility = visible;
}

/**
 * @brief Legend controller.
 * Do not own plot->legend, but control it elements content.
 */
class DataViewerLegend final {
  QCustomPlot                    *m_plot;
  QCPLegend                      *m_legend;
  std::vector<CustomLegendItem *> m_legendElements;
  bool                            m_valueVisible;

  QCPLayoutInset *m_timeLay;
  QCPTextElement *m_timeLabel;
  QCPTextElement *m_timeValue;

public:
  explicit DataViewerLegend(QCustomPlot *plot);
  void setStyles(QVariantList const &stateListVariant);
  void setStyle(int index, QVariantMap const &stateMapVariant);
  // each channel's value as text, by channel index (not the item's place in the grid)
  void setValueTexts(QStringList const &texts);
  void setVisible(bool value);
  // the time row and room for the values (unused: the chart's label at the mouse tells the time)
  void setValueVisible(bool visible);
  // room for a value after each name, without the time row
  void setItemValuesVisible(bool visible);
  void updateTimePosition();
  // visible channels only, `columns` per row (hidden ones after them, so they leave no gaps)
  void compactLayout(int columns);
};
}

#endif /* DATAVIEWERLEGEND_H */
