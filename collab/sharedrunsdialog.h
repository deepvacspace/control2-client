#ifndef SHAREDRUNSDIALOG_H
#define SHAREDRUNSDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace deepvaccontrol {

class CollabManager;

/**
 * @brief Experiments shared by the computers paired with the host; the ones with data can be
 * downloaded into this computer's experiment database (then shown in Data analysis).
 */
class SharedRunsDialog : public QDialog {
  Q_OBJECT
public:
  explicit SharedRunsDialog(CollabManager *collab, QWidget *parent = nullptr);

private:
  void refresh();

  CollabManager *m_collab;
  QTreeWidget   *m_list{nullptr};
  QLabel        *m_status{nullptr};
  QPushButton   *m_downloadBtn{nullptr};
  QPushButton   *m_syncBtn{nullptr};
};

}

#endif // SHAREDRUNSDIALOG_H
