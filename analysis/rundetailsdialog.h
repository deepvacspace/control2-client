#ifndef RUNDETAILSDIALOG_H
#define RUNDETAILSDIALOG_H

#include <QDialog>
#include <QPushButton>

#include "runmanifest.h"

class QLineEdit;
class QPlainTextEdit;
class QTimer;

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
  // caption always "Run details…" (the DUT is shown next to the button)
  void setPlainCaption(bool plain);

private:
  void refresh();
  bool m_plainCaption{false};

  ResHolder *m_resHolder;
};

// The same details as an inline form (Manual control page): edits are stored for the next runs as they are typed.
class RunDetailsPanel : public QWidget {
  Q_OBJECT
public:
  explicit RunDetailsPanel(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  void load();  // from the recorder
  void store(); // to the recorder

  ResHolder      *m_resHolder;
  QLineEdit      *m_dut{nullptr};
  QLineEdit      *m_operator{nullptr};
  QLineEdit      *m_purpose{nullptr};
  QLineEdit      *m_tags{nullptr};
  QPlainTextEdit *m_notes{nullptr};
  QTimer         *m_storeTimer{nullptr};
  bool            m_loading{false};
};

} // namespace deepvaccontrol

#endif // RUNDETAILSDIALOG_H
