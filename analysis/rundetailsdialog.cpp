#include "rundetailsdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "accessmanager.h"
#include "experimentrecorder.h"
#include "lineeditscreen.h"
#include "resholder.h"
#include "utils.h"

namespace deepvaccontrol {

RunDetailsDialog::RunDetailsDialog(core::RunMetadata const &metadata, QString const &title, QWidget *parent, bool withTagsAndNotes)
    : QDialog{parent}, m_initial{metadata} {
  setWindowTitle(title);
  setMinimumWidth(460 * utils::ssf());
  auto *form = new QFormLayout(this);
  m_dut      = new QLineEdit(metadata.dut, this);
  m_operator = new QLineEdit(metadata.operatorName, this);
  m_purpose  = new QLineEdit(metadata.purpose, this);
  m_tags     = new QLineEdit(metadata.tags.join(QStringLiteral(", ")), this);
  m_notes    = new QPlainTextEdit(metadata.notes, this);
  m_dut->setPlaceholderText(tr("Device under test, e.g. serial number"));
  m_purpose->setPlaceholderText(tr("e.g. thermal cycling qualification"));
  m_tags->setPlaceholderText(tr("comma separated, e.g. qualification, batch-7"));
  m_notes->setFixedHeight(80 * utils::ssf());
  for (QLineEdit *edit : {m_dut, m_operator, m_purpose, m_tags}) {
    edit->setMinimumHeight(32 * utils::ssf());
  }
  form->addRow(tr("DUT"), m_dut);
  form->addRow(tr("Operator"), m_operator);
  form->addRow(tr("Purpose"), m_purpose);
  if (withTagsAndNotes) {
    form->addRow(tr("Tags"), m_tags);
    form->addRow(tr("Notes"), m_notes);
  } else {
    m_tags->setVisible(false);
    m_notes->setVisible(false);
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  form->addRow(buttons);
}

core::RunMetadata RunDetailsDialog::metadata() const {
  core::RunMetadata metadata;
  metadata.dut          = m_dut->text().trimmed();
  metadata.operatorName = m_operator->text().trimmed();
  metadata.purpose      = m_purpose->text().trimmed();
  if (m_tags->isHidden()) {
    metadata.notes = m_initial.notes; // not edited here
    metadata.tags  = m_initial.tags;
    return metadata;
  }
  metadata.notes = m_notes->toPlainText().trimmed();
  for (QString const &tag : m_tags->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    if (!tag.trimmed().isEmpty()) {
      metadata.tags << tag.trimmed();
    }
  }
  return metadata;
}

RunDetailsButton::RunDetailsButton(ResHolder *resHolder, QWidget *parent) : QPushButton{tr("Experiment\ndetails…"), parent}, m_resHolder{resHolder} {
  setFixedSize(100 * utils::ssf(), 50 * utils::ssf());
  connect(this, &QPushButton::clicked, this, [this] {
    if (!m_resHolder->recorder) {
      return;
    }
    core::RunMetadata current = m_resHolder->recorder->nextRunMetadata();
    if (current.operatorName.isEmpty() && m_resHolder->users) {
      current.operatorName = m_resHolder->users->getCurrentUserName();
    }
    RunDetailsDialog dialog(current, tr("Experiment details"), this, false);
    if (dialog.exec() == QDialog::Accepted) {
      m_resHolder->recorder->setNextRunMetadata(dialog.metadata());
    }
  });
  if (m_resHolder->recorder) {
    connect(m_resHolder->recorder, &ExperimentRecorder::nextRunMetadataChanged, this, &RunDetailsButton::refresh);
  }
  refresh();
}

void RunDetailsButton::setPlainCaption(bool plain) {
  m_plainCaption = plain;
  refresh();
}

void RunDetailsButton::refresh() {
  core::RunMetadata const metadata = m_resHolder->recorder ? m_resHolder->recorder->nextRunMetadata() : core::RunMetadata{};
  // long DUT names are shortened in the middle so a serial number at the end stays readable
  QString const dut = fontMetrics().elidedText(metadata.dut, Qt::ElideMiddle, width() - static_cast<int>(10 * utils::ssf()));
  setText((metadata.dut.isEmpty() || m_plainCaption) ? (m_plainCaption ? tr("Experiment details…") : tr("Experiment\ndetails…"))
                                                      : tr("DUT\n%1").arg(dut));
  setToolTip(metadata.isEmpty() ? tr("DUT, operator, purpose and tags recorded with the next experiments")
                                : tr("Next experiments are recorded with:\nDUT: %1\nOperator: %2\nPurpose: %3\nTags: %4")
                                    .arg(metadata.dut, metadata.operatorName.isEmpty() ? tr("(logged-in user)") : metadata.operatorName,
                                         metadata.purpose, metadata.tags.join(QStringLiteral(", "))));
}

RunDetailsPanel::RunDetailsPanel(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(4 * utils::ssf());
  auto field = [&](QString const &title, QWidget *edit) {
    auto *label = new QLabel(title, this);
    label->setProperty("muted", true);
    lay->addWidget(label);
    lay->addWidget(edit);
    lay->addSpacing(6 * utils::ssf());
  };
  // on-screen keyboard on the HMI, like the other text fields
  auto line = [this](QString const &placeholder) {
    auto *edit = new LineEditScreen(this, VirtualKeyboard::EN);
    edit->setPlaceholderText(placeholder);
    edit->setMinimumHeight(30 * utils::ssf());
    return edit;
  };
  m_dut      = line(tr("Device under test, e.g. serial number"));
  m_operator = line(tr("Logged-in user"));
  m_purpose  = line(tr("e.g. thermal cycling qualification"));
  field(tr("DUT"), m_dut);
  field(tr("Operator"), m_operator);
  field(tr("Purpose"), m_purpose);

  m_storeTimer = new QTimer(this);
  m_storeTimer->setSingleShot(true);
  m_storeTimer->setInterval(400);
  connect(m_storeTimer, &QTimer::timeout, this, &RunDetailsPanel::store);
  for (QLineEdit *edit : {m_dut, m_operator, m_purpose}) {
    connect(edit, &QLineEdit::textChanged, this, [this] {
      if (!m_loading) {
        m_storeTimer->start();
      }
    });
  }
  if (m_resHolder->recorder) {
    connect(m_resHolder->recorder, &ExperimentRecorder::nextRunMetadataChanged, this, &RunDetailsPanel::load);
  }
  if (m_resHolder->users) {
    connect(m_resHolder->users, &AccessManager::userSelected, this,
            [this](QString const &name, AccessManager::AccessLevel) { userChanged(name); });
  }
  load();
}

void RunDetailsPanel::load() {
  if (!m_resHolder->recorder || m_storeTimer->isActive()) {
    return;
  }
  core::RunMetadata const metadata = m_resHolder->recorder->nextRunMetadata();
  m_loading                        = true;
  auto set                         = [](QLineEdit *edit, QString const &text) {
    if (edit->text() != text) {
      edit->setText(text);
    }
  };
  set(m_dut, metadata.dut);
  set(m_purpose, metadata.purpose);
  bool fillOperator{false};
  if (metadata.operatorName.isEmpty() && m_resHolder->users) {
    // default: the logged-in user
    m_autoOperator = m_resHolder->users->getCurrentUserName();
    set(m_operator, m_autoOperator);
    fillOperator = !m_autoOperator.isEmpty();
  } else {
    set(m_operator, metadata.operatorName);
  }
  m_loading = false;
  if (fillOperator) {
    store();
  }
}

void RunDetailsPanel::userChanged(QString const &name) {
  // the operator follows the logged-in user unless another name was typed
  QString const current = m_operator->text().trimmed();
  if (current.isEmpty() || (current == m_autoOperator)) {
    m_autoOperator = name;
    m_operator->setText(name); // stored by the timer
  }
}

void RunDetailsPanel::store() {
  if (!m_resHolder->recorder) {
    return;
  }
  core::RunMetadata metadata = m_resHolder->recorder->nextRunMetadata(); // tags and notes stay
  metadata.dut               = m_dut->text().trimmed();
  metadata.operatorName      = m_operator->text().trimmed();
  metadata.purpose           = m_purpose->text().trimmed();
  m_resHolder->recorder->setNextRunMetadata(metadata);
}

} // namespace deepvaccontrol
