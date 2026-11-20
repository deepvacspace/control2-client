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

// An experiment's details as a form, in the order of Control's first step: name or purpose, operator, DUT, the DUT's
// mass and density side by side; each title over its field. Control (RunDetailsPanel) and Analysis' Info panel.
class RunDetailsForm : public QWidget {
  Q_OBJECT
public:
  explicit RunDetailsForm(QWidget *parent = nullptr);

  // name or purpose and operator marked required (a red asterisk, "Required" when empty)
  void setRequired();
  void setReadOnly(bool readOnly);
  // the DUT, its mass and density only (name or purpose and operator hidden: given otherwise)
  void setDutOnly();
  // the fields from these details (edited() not emitted)
  void setMetadata(core::RunMetadata const &metadata);
  // these details with the fields' values (the others, tags and notes, kept)
  [[nodiscard]] core::RunMetadata applyTo(core::RunMetadata metadata) const;
  // the mass and the density are empty or numbers
  [[nodiscard]] bool valid() const;

  [[nodiscard]] QLineEdit *dut() const { return m_dut; }
  [[nodiscard]] QLineEdit *mass() const { return m_mass; }
  [[nodiscard]] QLineEdit *density() const { return m_density; }
  [[nodiscard]] QLineEdit *operatorName() const { return m_operator; }
  [[nodiscard]] QLineEdit *purpose() const { return m_purpose; }

signals:
  void edited(); // a field changed by the user

private:
  QLineEdit *m_dut{nullptr};
  QLineEdit *m_mass{nullptr};    // kg
  QLineEdit *m_density{nullptr}; // kg/m³
  QLineEdit *m_operator{nullptr};
  QLineEdit *m_purpose{nullptr};
  QLabel    *m_operatorTitle{nullptr};
  QLabel    *m_purposeTitle{nullptr};
  bool       m_loading{false};
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
  // no name given, or a default one: "Untitled" and the date and time now ("Untitled 2026-11-06 10:30")
  void fillDefaultName();

signals:
  void edited(); // a field changed (stored a moment later)

private:
  void load();  // from the recorder
  void store(); // to the recorder
  void userChanged(QString const &name);

  ResHolder      *m_resHolder;
  RunDetailsForm *m_form{nullptr};
  // its fields
  QLineEdit *m_dut{nullptr};
  QLineEdit *m_mass{nullptr};
  QLineEdit *m_density{nullptr};
  QLineEdit *m_operator{nullptr};
  QLineEdit *m_purpose{nullptr};
  QTimer    *m_storeTimer{nullptr};
  QString    m_autoOperator; // the logged-in user filled in as operator
  bool       m_loading{false};
};

}

#endif // RUNDETAILSDIALOG_H
