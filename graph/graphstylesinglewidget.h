#ifndef GRAPHSTYLESINGLEWIDGET_H
#define GRAPHSTYLESINGLEWIDGET_H

#include <QComboBox>
#include <QPushButton>
#include <QFrame>
#include <QLabel>
#include <QWidget>
#include <QCheckBox>

namespace deepvaccontrol {

// One channel in the Channels dialog: shown or not, colour, name, line style and width.
class GraphStyleSingleWidget : public QFrame {
  Q_OBJECT
public:
  // narrower line style and width boxes (side panel)
  void setCompact();
  explicit GraphStyleSingleWidget(int index, QVariantMap const &stateMap, QWidget *parent = nullptr);
  QVariantMap getStateMapVariant() const; // =getStyle;
  void        setEnabled(bool value);
  // back to a style kept before (shown, colour, line style and width), without styleUpdated
  void        setStateMap(QVariantMap const &stateMap);

signals:
  void styleUpdated();

private slots:
  void handleColorSelected(QColor newColor);

private:
  void updateLineStyleBox();
  void updateSwatch();

  int         m_index;
  QVariantMap m_stateMap;

  QCheckBox   *m_visibility;
  QLabel      *m_label{nullptr};
  QPushButton *m_colorPicker;
  QComboBox   *m_lineStyle;
  int          m_lineSample{80}; // width of the line drawn in the style box (unscaled px)
  QComboBox   *m_lineWidth;
  QWidget     *m_styleBox{nullptr}; // compact: the line style and width, under the name
};

inline QVariantMap GraphStyleSingleWidget::getStateMapVariant() const {
  return m_stateMap;
}
}
#endif // GRAPHSTYLESINGLEWIDGET_H
