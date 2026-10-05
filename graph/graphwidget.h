#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "dataviewer.h"
#include "graphstyleeditor.h"
#include "resholder.h"

namespace deepvaccontrol {
class GraphWidget : public QWidget {
  Q_OBJECT
public:
  explicit GraphWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  void updateModeSelectButton(DataViewer::ControlMode mode);
  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);

private:
  void              exportData();
  // time and values in the read-out box: at the cursor (cursor on) or the latest values
  void              updateReadout(double cursorKey = std::numeric_limits<double>::quiet_NaN());
  void              updateDayButton();
  QPushButton      *m_readoutBtn{nullptr};
  QPushButton      *m_dayBtn{nullptr};
  ResHolder        *m_resHolder;
  DataViewer       *m_dataViewer;
  GraphStyleEditor *m_styleEditor;
  QPushButton      *m_modeSelectBtn;
  QPushButton      *m_exportToBtn;
  QPushButton      *m_styleBtn;
};
} // namespace deepvaccontrol

#endif // GRAPHWIDGET_H
