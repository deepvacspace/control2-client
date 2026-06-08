#ifndef GRAPHSTYLESINGLEWIDGET_H
#define GRAPHSTYLESINGLEWIDGET_H

#include <QComboBox>
#include <QPushButton>
#include <QWidget>
#include <QCheckBox>

namespace control2 {

class GraphStyleSingleWidget : public QWidget {
  Q_OBJECT
public:
  explicit GraphStyleSingleWidget(int index, QVariantMap const &stateMap, QWidget *parent = nullptr);
  QVariantMap getStateMapVariant() const; // =getStyle;
  void        setEnabled(bool value);

signals:
  void styleUpdated();

private slots:
  void handleColorSelected(QColor newColor);

private:
  void updateLineStyleBox();

  int         m_index;
  QVariantMap m_stateMap;

  QCheckBox   *m_visibility;
  QPushButton *m_colorPicker;
  QComboBox   *m_lineStyle;
  QComboBox   *m_lineWidth;
};

inline QVariantMap GraphStyleSingleWidget::getStateMapVariant() const {
  return m_stateMap;
}
} // namespace control2
#endif // GRAPHSTYLESINGLEWIDGET_H
