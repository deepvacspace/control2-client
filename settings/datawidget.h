#ifndef DATAWIDGET_H
#define DATAWIDGET_H

#include <QWidget>

class QTreeWidget;

namespace deepvaccontrol {

struct ResHolder;

/**
 * @brief Settings tab for the app's databases (Deepvac Insight format) and their backups:
 * back up now, see the daily backups, restore one.
 */
class DataWidget : public QWidget {
  Q_OBJECT
public:
  explicit DataWidget(ResHolder *resHolder, QWidget *parent = nullptr);

protected:
  void showEvent(QShowEvent *event) override;

private:
  void refresh();
  void backupNow();
  void restoreSelected();

  ResHolder   *m_resHolder;
  QTreeWidget *m_tree{nullptr};
};

} // namespace deepvaccontrol

#endif // DATAWIDGET_H
