#ifndef CYCLEWIDGET_H
#define CYCLEWIDGET_H

#include <QLabel>
#include <QWidget>

namespace deepvaccontrol {
class CycleWidget : public QWidget {
  Q_OBJECT
public:
  explicit CycleWidget(QWidget *parent = nullptr);
  void        setParams(QVariantMap const &newParams);
  void        setIntervalsCount(int i);
  QVariantMap getParams() const;

signals:
  void paramsChanged(QVariantMap newParams);

private:
  void        initGui();
  void        updateGui();
  QVariantMap emptyCycleMap();

  QVariantMap m_params;
  int         m_intervalNumber;
  QLabel     *m_text;
};

inline QVariantMap CycleWidget::getParams() const {
  return m_params;
}
} // namespace deepvaccontrol

#endif // CYCLEWIDGET_H
