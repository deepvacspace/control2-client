#ifndef GRAPHWIDGET_H
#define GRAPHWIDGET_H

#include <QPushButton>
#include <QWidget>

#include "dataviewer.h"
#include "graphstyleeditor.h"
#include "resholder.h"

namespace control2 {
class GraphWidget : public QWidget {
  Q_OBJECT
public:
  explicit GraphWidget(ResHolder *resHolder, QWidget *parent = nullptr);

  void updateModeSelectButton(DataViewer::ControlMode mode);
  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);

private:
  void              exportData();
  ResHolder        *m_resHolder;
  DataViewer       *m_dataViewer;
  GraphStyleEditor *m_styleEditor;
  QPushButton      *m_modeSelectBtn;
  QPushButton      *m_exportToBtn;
  QPushButton      *m_styleBtn;
};
} // namespace control2

#endif // GRAPHWIDGET_H
