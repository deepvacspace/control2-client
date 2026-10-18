#ifndef CHARTSTATSPANEL_H
#define CHARTSTATSPANEL_H

#include <QPointer>
#include <QVariantList>
#include <QVector>
#include <QWidget>

class QComboBox;
class QFrame;
class QLabel;
class QTimer;

namespace deepvaccontrol {
class DataViewer;

/**
 * @brief Chart page side panel: statistics of the visible range of one channel and the control against the setpoint
 * (temp against temp_ref), following the zoom. The full statistics (stages, anomalies, events, rules) are a tap away.
 */
class ChartStatsPanel : public QWidget {
  Q_OBJECT
public:
  ChartStatsPanel(QStringList const &names, QVariantList const &styles, QWidget *parent = nullptr);

  // the chart followed; a comparison has no single experiment to describe here
  void setSource(DataViewer *viewer, bool comparison);

signals:
  void fullReportRequested();

protected:
  void showEvent(QShowEvent *event) override;

private:
  void refresh();
  // a rounded box of name / value rows; the value labels in order
  QFrame *makeBox(QStringList const &names, QVector<QLabel *> &values);

  QStringList          m_names;
  QVariantList         m_styles;
  QPointer<DataViewer> m_viewer;
  bool                 m_comparison{false};
  QTimer              *m_timer{nullptr};
  QLabel              *m_rangeLabel{nullptr};
  QComboBox           *m_channel{nullptr};
  QFrame              *m_channelBox{nullptr};
  QLabel              *m_controlCaption{nullptr};
  QFrame              *m_controlBox{nullptr};
  QLabel              *m_note{nullptr};
  QVector<QLabel *>    m_channelValues;
  QVector<QLabel *>    m_controlValues;
  QMetaObject::Connection m_rangeConnection;
  QMetaObject::Connection m_dataConnection;
};

} // namespace deepvaccontrol

#endif // CHARTSTATSPANEL_H
