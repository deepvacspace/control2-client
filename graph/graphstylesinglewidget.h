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
  explicit GraphStyleSingleWidget(int index, QVariantMap const &stateMap, QWidget *parent = nullptr);
  QVariantMap getStateMapVariant() const; // =getStyle;
  void        setEnabled(bool value);

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
  QComboBox   *m_lineWidth;
};

inline QVariantMap GraphStyleSingleWidget::getStateMapVariant() const {
  return m_stateMap;
}
} // namespace deepvaccontrol
#endif // GRAPHSTYLESINGLEWIDGET_H
