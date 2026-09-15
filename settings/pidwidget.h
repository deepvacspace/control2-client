#ifndef PIDWIDGET_H
#define PIDWIDGET_H

#include <QWidget>

#include "lineeditnum.h"

namespace deepvaccontrol {
class PidWidget : public QWidget {
  Q_OBJECT
public:
  explicit PidWidget(QVariantMap data, QWidget *parent = nullptr);
  ~PidWidget() override = default;
  void        setDataVariant(QVariantMap data);
  QVariantMap getDataVariant() const;

private:
  void initGui();

  QString m_name;
  QString m_label;
  quint32 m_points{0U};
  float   m_minPoint{0.0f};
  float   m_maxPoint{0.0f};

  // All numedit to delete in reinitGui()
  // all p-gains, then i-times, then d-times
  QList<LineEditNum *> m_fields;
};
} // namespace deepvaccontrol

#endif // PIDWIDGET_H
