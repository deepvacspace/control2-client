#ifndef GRAPHSTYLEEDITOR_H
#define GRAPHSTYLEEDITOR_H

#include <QDialog>
#include <graphstylesinglewidget.h>
#include <resholder.h>

namespace deepvaccontrol {
class GraphStyleEditor : public QDialog {
  Q_OBJECT
public:
  explicit GraphStyleEditor(ResHolder *resHolder, QWidget *parent = nullptr);
  QVariantList getStateStylesList(); // to call on hide/select all/unselect all

signals:
  void styleChanged(int index, QVariantMap stateMap);
  void styleAllChanged(QVariantList statesMapVariant);
private:
private:
  ResHolder                          *m_resHolder;
  QMap<int, GraphStyleSingleWidget *> widgetsMap; // only visible states

  // QWidget interface
protected:
  void hideEvent(QHideEvent *event) override;
};
} // namespace deepvaccontrol

#endif // GRAPHSTYLEEDITOR_H
