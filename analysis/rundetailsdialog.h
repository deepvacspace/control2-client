#ifndef RUNDETAILSDIALOG_H
#define RUNDETAILSDIALOG_H

#include <QDialog>
#include <QPushButton>

#include "runmanifest.h"

class QLineEdit;
class QPlainTextEdit;

namespace deepvaccontrol {

struct ResHolder;

// Run details (DUT, operator, purpose, tags, notes) of the next run or of a saved one.
class RunDetailsDialog : public QDialog {
  Q_OBJECT
public:
  RunDetailsDialog(core::RunMetadata const &metadata, QString const &title, QWidget *parent = nullptr);

  [[nodiscard]] core::RunMetadata metadata() const;

private:
  QLineEdit      *m_dut{nullptr};
  QLineEdit      *m_operator{nullptr};
  QLineEdit      *m_purpose{nullptr};
  QLineEdit      *m_tags{nullptr};
  QPlainTextEdit *m_notes{nullptr};
};

// "Run details…" next to the Start buttons: what the next runs are recorded with (kept until changed).
class RunDetailsButton : public QPushButton {
  Q_OBJECT
public:
  explicit RunDetailsButton(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  void refresh();

  ResHolder *m_resHolder;
};

} // namespace deepvaccontrol

#endif // RUNDETAILSDIALOG_H
