#ifndef DATAVIEWERLEGEND_H
#define DATAVIEWERLEGEND_H

#include "qcustomplot.h"

namespace control2 {
/**
 * @brief Custom element for legend with value textbox
 */
class CustomLegendItem : public QCPPlottableLegendItem {
  Q_OBJECT
public:
  CustomLegendItem(QCPLegend *parent, QString const &label, QPen const &iconPen);
  void setValue(double value);
  void setValueVisible(bool value);
  void setPen(QPen const &pen);

  // // getters:
  // QCPAbstractPlottable *plottable() { return mPlottable; }

protected:
  void drawLegendIcon(QCPPainter *painter, QRectF const &rect);

  // property members:
  QString m_label;
  double  m_value;
  bool    m_value_visibility;
  size_t  m_value_paddding;
  size_t  m_label_paddding;
  QPen    m_iconPen;

  // reimplemented virtual methods:
  virtual void  draw(QCPPainter *painter) override;
  virtual QSize minimumOuterSizeHint() const override;
};

inline void CustomLegendItem::setValue(double value) {
  m_value = value;
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
  void setValues(QDateTime const &timeStamp, QVector<double> const &values);
  void setVisible(bool value);
  void setValueVisible(bool visible);
  void updateTimePosition();
};
} // namespace control2

#endif /* DATAVIEWERLEGEND_H */
