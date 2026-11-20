#include "rundetailsdialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

#include "accessmanager.h"
#include "experimentrecorder.h"
#include "lineeditscreen.h"
#include "resholder.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
// a number field for the DUT's mass or density: empty when not given
void numberField(QLineEdit *edit, double value, QString const &example) {
  auto *validator = new QDoubleValidator(0.0, 1e9, 6, edit);
  validator->setNotation(QDoubleValidator::StandardNotation);
  validator->setLocale(QLocale::c());
  edit->setValidator(validator);
  edit->setPlaceholderText(example);
  edit->setText(std::isfinite(value) ? QLocale::c().toString(value, 'g', 6) : QString{});
}
}

RunDetailsDialog::RunDetailsDialog(core::RunMetadata const &metadata, QString const &title, QWidget *parent, bool withTagsAndNotes)
    : QDialog{parent}, m_initial{metadata} {
  setWindowTitle(title);
  setMinimumWidth(theme::dp(862));
  auto *form = new QFormLayout(this);
  m_dut      = new QLineEdit(metadata.dut, this);
  m_mass     = new QLineEdit(this);
  m_density  = new QLineEdit(this);
  numberField(m_mass, metadata.dutMassKg, tr("e.g. 12.5"));
  numberField(m_density, metadata.dutDensityKgM3, tr("e.g. 2700"));
  m_operator = new QLineEdit(metadata.operatorName, this);
  m_purpose  = new QLineEdit(metadata.purpose, this);
  m_tags     = new QLineEdit(metadata.tags.join(QStringLiteral(", ")), this);
  m_notes    = new QPlainTextEdit(metadata.notes, this);
  m_dut->setPlaceholderText(tr("Device under test, e.g. serial number"));
  m_purpose->setPlaceholderText(tr("e.g. thermal cycling qualification"));
  m_tags->setPlaceholderText(tr("comma separated, e.g. qualification, batch-7"));
  m_notes->setFixedHeight(theme::dp(150));
  for (QLineEdit *edit : {m_dut, m_mass, m_density, m_operator, m_purpose, m_tags}) {
    edit->setMinimumHeight(theme::dp(60));
  }
  form->addRow(tr("DUT"), m_dut);
  form->addRow(tr("DUT mass, kg"), m_mass);
  form->addRow(tr("DUT density, kg/m³"), m_density);
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
  metadata.dut            = m_dut->text().trimmed();
  metadata.dutMassKg      = core::RunMetadata::parseNumber(m_mass->text());
  metadata.dutDensityKgM3 = core::RunMetadata::parseNumber(m_density->text());
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
  setFixedSize(theme::dp(187), theme::dp(93));
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
                                : tr("Next experiments are recorded with:\nDUT: %1\nDUT mass: %2\nDUT density: %3\nOperator: %4\nPurpose: %5\nTags: %6")
                                    .arg(metadata.dut, metadata.dutMassText().isEmpty() ? QStringLiteral("—") : metadata.dutMassText(),
                                         metadata.dutDensityText().isEmpty() ? QStringLiteral("—") : metadata.dutDensityText(),
                                         metadata.operatorName.isEmpty() ? tr("(logged-in user)") : metadata.operatorName,
                                         metadata.purpose, metadata.tags.join(QStringLiteral(", "))));
}

RunDetailsForm::RunDetailsForm(QWidget *parent) : QWidget{parent} {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(theme::dp(8));
  auto title = [this](QString const &text) {
    auto *label = new QLabel(text, this);
    label->setProperty("muted", true);
    return label;
  };
  auto field = [&](QString const &text, QWidget *edit) {
    QLabel *label = title(text);
    lay->addWidget(label);
    lay->addWidget(edit);
    lay->addSpacing(theme::dp(8)); // 16 to the next title
    return label;
  };
  // on-screen keyboard on the HMI, like the other text fields
  auto line = [this](QString const &placeholder) {
    auto *edit = new LineEditScreen(this, VirtualKeyboard::EN);
    edit->setPlaceholderText(placeholder);
    edit->setMinimumHeight(theme::dp(56));
    return edit;
  };
  m_purpose  = line(tr("e.g. thermal cycling qualification"));
  m_operator = line(tr("Logged-in user"));
  m_dut      = line(tr("Device under test, e.g. serial number"));
  m_mass     = line(QString{});
  m_density  = line(QString{});
  numberField(m_mass, std::numeric_limits<double>::quiet_NaN(), tr("e.g. 12.5"));
  numberField(m_density, std::numeric_limits<double>::quiet_NaN(), tr("e.g. 2700"));
  m_purposeTitle  = field(tr("Name or purpose"), m_purpose);
  m_operatorTitle = field(tr("Operator"), m_operator);
  field(tr("DUT"), m_dut);
  // mass and density side by side
  auto *row = new QHBoxLayout();
  row->setSpacing(theme::gap());
  for (auto const &[text, edit] : {std::pair{tr("DUT mass, kg"), m_mass}, std::pair{tr("DUT density, kg/m³"), m_density}}) {
    auto *column = new QVBoxLayout();
    column->setSpacing(theme::dp(8));
    column->addWidget(title(text));
    column->addWidget(edit);
    row->addLayout(column, 1);
  }
  lay->addLayout(row);
  for (QLineEdit *edit : {m_purpose, m_operator, m_dut, m_mass, m_density}) {
    connect(edit, &QLineEdit::textChanged, this, [this] {
      if (!m_loading) {
        emit edited();
      }
    });
  }
}

void RunDetailsForm::setRequired() {
  // a red asterisk after the title
  QString const required = QStringLiteral(" <span style='color:%1'>*</span>").arg(theme::colors().danger.name());
  m_purposeTitle->setText(tr("Name or purpose").toHtmlEscaped() + required);
  m_operatorTitle->setText(tr("Operator").toHtmlEscaped() + required);
  for (QLineEdit *edit : {m_purpose, m_operator}) {
    edit->setPlaceholderText(tr("Required"));
  }
}

void RunDetailsForm::setDutOnly() {
  for (QWidget *widget : std::initializer_list<QWidget *>{m_purposeTitle, m_purpose, m_operatorTitle, m_operator}) {
    widget->hide();
  }
  // their spacings out too: the DUT's title first
  QLayout *lay = layout();
  int      i{0};
  while (i < lay->count()) {
    QLayoutItem *item = lay->itemAt(i);
    if (item->widget() && !item->widget()->isHidden()) {
      break;
    }
    if (item->spacerItem()) {
      delete lay->takeAt(i);
    } else {
      ++i;
    }
  }
}

void RunDetailsForm::setReadOnly(bool readOnly) {
  for (QLineEdit *edit : {m_purpose, m_operator, m_dut, m_mass, m_density}) {
    edit->setReadOnly(readOnly);
  }
}

void RunDetailsForm::setMetadata(core::RunMetadata const &metadata) {
  m_loading = true;
  m_purpose->setText(metadata.purpose);
  m_operator->setText(metadata.operatorName);
  m_dut->setText(metadata.dut);
  for (auto const &[edit, value] : {std::pair{m_mass, metadata.dutMassKg}, std::pair{m_density, metadata.dutDensityKgM3}}) {
    edit->setText(std::isfinite(value) ? QLocale::c().toString(value, 'g', 6) : QString{});
  }
  m_loading = false;
}

core::RunMetadata RunDetailsForm::applyTo(core::RunMetadata metadata) const {
  metadata.dut            = m_dut->text().trimmed();
  metadata.dutMassKg      = core::RunMetadata::parseNumber(m_mass->text());
  metadata.dutDensityKgM3 = core::RunMetadata::parseNumber(m_density->text());
  metadata.operatorName   = m_operator->text().trimmed();
  metadata.purpose        = m_purpose->text().trimmed();
  return metadata;
}

bool RunDetailsForm::valid() const {
  for (QLineEdit const *edit : {m_mass, m_density}) {
    if (!edit->text().trimmed().isEmpty() && !std::isfinite(core::RunMetadata::parseNumber(edit->text()))) {
      return false;
    }
  }
  return true;
}

RunDetailsPanel::RunDetailsPanel(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  m_form = new RunDetailsForm(this);
  lay->addWidget(m_form);
  m_dut      = m_form->dut();
  m_mass     = m_form->mass();
  m_density  = m_form->density();
  m_operator = m_form->operatorName();
  m_purpose  = m_form->purpose();

  m_storeTimer = new QTimer(this);
  m_storeTimer->setSingleShot(true);
  m_storeTimer->setInterval(400);
  connect(m_storeTimer, &QTimer::timeout, this, &RunDetailsPanel::store);
  for (QLineEdit *edit : {m_dut, m_mass, m_density, m_operator, m_purpose}) {
    connect(edit, &QLineEdit::textChanged, this, [this] {
      if (!m_loading) {
        m_storeTimer->start();
      }
      emit edited();
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
  // a number as typed stays (the stored one is the same value)
  for (auto const &[edit, value] : {std::pair{m_mass, metadata.dutMassKg}, std::pair{m_density, metadata.dutDensityKgM3}}) {
    double const shown{core::RunMetadata::parseNumber(edit->text())};
    if (!((std::isnan(shown) && std::isnan(value)) || (shown == value))) {
      set(edit, std::isfinite(value) ? QLocale::c().toString(value, 'g', 6) : QString{});
    }
  }
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

void RunDetailsPanel::setRequiredFields() {
  m_form->setRequired();
}

bool RunDetailsPanel::complete() const {
  return !m_purpose->text().trimmed().isEmpty() && !m_operator->text().trimmed().isEmpty();
}

void RunDetailsPanel::fillDefaultName() {
  // a default given before (also in an earlier session) follows the time the page is opened; a name typed stays
  QString const            untitled = tr("Untitled");
  QRegularExpression const pattern{QStringLiteral("^%1 \\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}$").arg(QRegularExpression::escape(untitled))};
  QString const            current = m_purpose->text().trimmed();
  if (!current.isEmpty() && !pattern.match(current).hasMatch()) {
    return;
  }
  QString const name = QStringLiteral("%1 %2").arg(untitled, QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
  if (name != current) {
    m_purpose->setText(name);
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
  metadata.dutMassKg         = core::RunMetadata::parseNumber(m_mass->text());
  metadata.dutDensityKgM3    = core::RunMetadata::parseNumber(m_density->text());
  metadata.operatorName      = m_operator->text().trimmed();
  metadata.purpose           = m_purpose->text().trimmed();
  m_resHolder->recorder->setNextRunMetadata(metadata);
}

}
