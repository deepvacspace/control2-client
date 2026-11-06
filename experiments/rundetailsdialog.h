#ifndef RUNDETAILSDIALOG_H
#define RUNDETAILSDIALOG_H

#include <QDialog>
#include <QPushButton>

#include "runmanifest.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimer;

namespace deepvaccontrol {

struct ResHolder;

// Run details (DUT with its mass and density, operator, purpose, tags, notes) of the next run or of a saved one.
class RunDetailsDialog : public QDialog {
  Q_OBJECT
public:
  // withTagsAndNotes false: only DUT, operator and purpose are shown; tags and notes are kept as they are
  RunDetailsDialog(core::RunMetadata const &metadata, QString const &title, QWidget *parent = nullptr, bool withTagsAndNotes = true);

  [[nodiscard]] core::RunMetadata metadata() const;

private:
  QLineEdit      *m_dut{nullptr};
  QLineEdit      *m_mass{nullptr};    // kg
  QLineEdit      *m_density{nullptr}; // kg/m³
  QLineEdit      *m_operator{nullptr};
  QLineEdit      *m_purpose{nullptr};
  QLineEdit      *m_tags{nullptr};
  QPlainTextEdit *m_notes{nullptr};
  core::RunMetadata m_initial;
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

// DUT, operator and purpose as an inline form (Program page, step 1): edits are stored for the next experiments as
// they are typed (tags and notes are kept as they are). The operator is the logged-in user unless typed otherwise.
class RunDetailsPanel : public QWidget {
  Q_OBJECT
public:
  explicit RunDetailsPanel(ResHolder *resHolder, QWidget *parent = nullptr);

  // the Program page's form: the purpose is "Name or purpose", first; it and the operator say "Required" when empty
  void setRequiredFields();
  [[nodiscard]] bool complete() const; // name or purpose and operator given

signals:
  void edited(); // a field changed (stored a moment later)

private:
  void load();  // from the recorder
  void store(); // to the recorder
  void userChanged(QString const &name);

  ResHolder *m_resHolder;
  QLineEdit *m_dut{nullptr};
  QLineEdit *m_mass{nullptr};
  QLineEdit *m_density{nullptr};
  QLineEdit *m_operator{nullptr};
  QLineEdit *m_purpose{nullptr};
  QLabel    *m_operatorTitle{nullptr};
  QLabel    *m_purposeTitle{nullptr};
  QTimer    *m_storeTimer{nullptr};
  QString    m_autoOperator; // the logged-in user filled in as operator
  bool       m_loading{false};
};

}

#endif // RUNDETAILSDIALOG_H
