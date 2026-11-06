#ifndef GRAPHSTYLEEDITOR_H
#define GRAPHSTYLEEDITOR_H

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <graphstylesinglewidget.h>
#include <resholder.h>

namespace deepvaccontrol {
class GraphStyleEditor : public QDialog {
  Q_OBJECT
public:
  explicit GraphStyleEditor(ResHolder *resHolder, QWidget *parent = nullptr);
  // its own choice, kept under settingsKey (the channels' "states" give the rest); before one is kept, only the
  // defaultShown channels (state names) are shown
  GraphStyleEditor(ResHolder *resHolder, QString const &settingsKey, QStringList const &defaultShown, QWidget *parent = nullptr);
  QVariantList getStateStylesList(); // to call on hide/select all/unselect all
  // inside the Analysis page's side panel: no title or Done, narrower line style boxes, not a window
  void setCompact();
  // exactly these channels shown (state names, or labels without their unit), the others hidden
  void showOnly(QStringList const &channels);
  // "6 of 8 shown"
  [[nodiscard]] QString countText() const {
    return m_countLabel->text();
  }

signals:
  void styleChanged(int index, QVariantMap stateMap);
  void styleAllChanged(QVariantList statesMapVariant);
  // "6 of 8 shown", whenever it changes (compact: shown by the side panel under its title)
  void countChanged(QString const &text);
private:
private:
  ResHolder                          *m_resHolder;
  QString                             m_settingsKey{QStringLiteral("states")};
  QVariantList                        m_styles; // the channels' styles, the kept choice over them, as opened
  QMap<int, GraphStyleSingleWidget *> widgetsMap; // only visible states
  QLabel                             *m_countLabel{nullptr};
  QPushButton                        *m_showAllBtn{nullptr};
  QPushButton                        *m_hideAllBtn{nullptr};
  QHBoxLayout                        *m_footerLay{nullptr};
  void                                updateCount();

  // QWidget interface
protected:
  void hideEvent(QHideEvent *event) override;
};
}

#endif // GRAPHSTYLEEDITOR_H
