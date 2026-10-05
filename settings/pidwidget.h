#ifndef PIDWIDGET_H
#define PIDWIDGET_H

#include <QWidget>

#include "lineeditnum.h"
#include "systemvalue.h"

class QFrame;

namespace deepvaccontrol {
class BandScale;

// PID gains per temperature band (a table of editable gains) and a scale of the bands with the setpoint and the
// current temperature; the PLC chooses the band from the setpoint, so the setpoint's band is highlighted in both.
class PidWidget : public QWidget {
  Q_OBJECT
public:
  explicit PidWidget(QVariantMap data, QWidget *parent = nullptr);
  ~PidWidget() override = default;
  void        setDataVariant(QVariantMap data);
  QVariantMap getDataVariant() const;
  // live values shown on the band scale: the setpoint selects the band, the temperature is shown with it
  void        setCurrentValue(SystemValue *value);
  void        setSetpointValue(SystemValue *value);

private:
  void initGui();
  void updateActiveBand();

  QString m_name;
  QString m_label;
  quint32 m_points{0U};
  float   m_minPoint{0.0f};
  float   m_maxPoint{0.0f};

  // All numedit to delete in reinitGui()
  // all p-gains, then i-times, then d-times
  QList<LineEditNum *> m_fields;
  QVector<QFrame *>    m_rows; // band i (lowest range first)
  BandScale           *m_scale{nullptr};
  SystemValue         *m_current{nullptr};
  SystemValue         *m_setpoint{nullptr};
};
} // namespace deepvaccontrol

#endif // PIDWIDGET_H
