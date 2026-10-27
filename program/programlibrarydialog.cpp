#include "programlibrarydialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QGuiApplication>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QStandardItem>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

#include "constants.h"
#include "dialogs/confdialog.h"
#include "dialogs/messagedialog.h"
#include "experimentlibrary.h"
#include "lineeditscreen.h"
#include "programmodel.h"
#include "utils.h"

namespace deepvaccontrol {

using storage::TestProfile;
using storage::TestProfilesStore;
using storage::TestProfileStep;

namespace {
// as large as wanted, but never beyond the screen it opens on
void fitToScreen(QDialog *dialog, QWidget *parent, int width, int height) {
  QScreen const *screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
  QRect const    room   = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
  dialog->resize(std::min(static_cast<int>(width * utils::ssf()), room.width() * 9 / 10),
                 std::min(static_cast<int>(height * utils::ssf()), room.height() * 85 / 100));
  dialog->setMaximumSize(room.width() * 95 / 100, room.height() * 90 / 100);
}

QPushButton *makeButton(QString const &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setFixedHeight(deepvaccontrol::UI_BTN_HEIGHT * utils::ssf());
  button->setMinimumWidth(110 * utils::ssf());
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}

QString setpointsText(TestProfileStep const &step) {
  QStringList parts;
  if (!std::isnan(step.setpointTemp)) {
    parts << QObject::tr("%1 °C").arg(step.setpointTemp);
  }
  if (!std::isnan(step.setpointPressure)) {
    parts << QObject::tr("pressure %1").arg(step.setpointPressure);
  }
  return parts.isEmpty() ? QObject::tr("other channels") : parts.join(", ");
}
}

namespace programlibrary {

QVector<TestProfileStep> stepsFromModel(ProgramModel const &model, QStringList const &labels) {
  QVariantList const intervals = model.programVariant()["intervals"].toList();
  QStringList const  params    = model.getParamListOrder();
  int const          tempBit{static_cast<int>(params.indexOf("temp"))};
  int const          presBit{static_cast<int>(params.indexOf("pres"))};

  QVector<TestProfileStep> steps;
  for (int i{0}; i < intervals.size(); ++i) {
    QVariantMap const interval = intervals[i].toMap();
    quint32 const     flags{interval["flags"].toUInt()};
    TestProfileStep   step;
    step.durationS        = interval["duration"].toDouble();
    step.setpointTemp     = ((tempBit >= 0) && utils::getBitOfDword(flags, tempBit)) ? interval["temp"].toDouble() : qQNaN();
    step.setpointPressure = ((presBit >= 0) && utils::getBitOfDword(flags, presBit)) ? interval["pres"].toDouble() : qQNaN();
    step.label            = labels.value(i);
    steps.push_back(step);
  }
  return steps;
}

bool loadIntoModel(TestProfile const &profile, ProgramModel &model, QString *error) {
  if (!profile.programJson.isEmpty()) {
    QVariantMap const program = QJsonDocument::fromJson(profile.programJson.toUtf8()).toVariant().toMap();
    if (model.setProgramVariant(program)) {
      return true;
    }
    if (error) {
      *error = QObject::tr("'%1' was saved for a chamber with other control channels.").arg(profile.name);
    }
    return false;
  }

  // steps only (e.g. created by an older version): one interval per step, setpoint held for its duration
  QStringList const params = model.getParamListOrder();
  int const         tempBit{static_cast<int>(params.indexOf("temp"))};
  int const         presBit{static_cast<int>(params.indexOf("pres"))};
  QVariantMap       defaults;
  for (QVariant const &param : model.getParamListVariant()) {
    defaults.insert(param.toMap()["name"].toString(), param.toMap()["value"]);
  }

  QVariantList intervals;
  for (int i{0}; i < profile.steps.size(); ++i) {
    TestProfileStep const &step = profile.steps[i];
    if ((!std::isnan(step.setpointTemp) && (tempBit < 0)) || (!std::isnan(step.setpointPressure) && (presBit < 0))) {
      if (error) {
        *error = QObject::tr("Step %1 uses a setpoint this chamber cannot control.").arg(i + 1);
      }
      return false;
    }
    QVariantMap interval = defaults;
    quint32     flags{0U};
    if (!std::isnan(step.setpointTemp)) {
      interval["temp"] = step.setpointTemp;
      utils::setBitOfDword(flags, tempBit, true);
    }
    if (!std::isnan(step.setpointPressure)) {
      interval["pres"] = step.setpointPressure;
      utils::setBitOfDword(flags, presBit, true);
    }
    interval["duration"] = std::round(step.durationS);
    interval["flags"]    = flags;
    intervals.push_back(interval);
  }
  QVariantMap program;
  program["intervals"] = intervals;
  program["cycle"]     = QVariantMap{{"start", 0}, {"end", 0}, {"count", 0}};
  return model.setProgramVariant(program);
}

}

// ---------------------------------------------------------------- open

ProgramLibraryDialog::ProgramLibraryDialog(QWidget *parent) : QDialog{parent} {
  setWindowTitle(tr("Program library"));
  fitToScreen(this, parent, 900, 560);

  auto *root = new QVBoxLayout(this);
  auto *body = new QHBoxLayout();
  m_list     = new QListWidget(this);
  m_preview = new QLabel(this);
  m_preview->setWordWrap(true);
  m_preview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_preview->setMinimumWidth(320 * utils::ssf());
  body->addWidget(m_list, 3);
  body->addWidget(m_preview, 2);
  root->addLayout(body, 1);

  auto *buttons  = new QHBoxLayout();
  m_openBtn      = makeButton(tr("Open"), this);
  m_deleteBtn    = makeButton(tr("Delete"), this);
  auto *closeBtn = makeButton(tr("Close"), this);
  buttons->addWidget(m_openBtn);
  buttons->addWidget(m_deleteBtn);
  buttons->addStretch(1);
  buttons->addWidget(closeBtn);
  root->addLayout(buttons);

  connect(m_list, &QListWidget::currentRowChanged, this, &ProgramLibraryDialog::showPreview);
  connect(m_list, &QListWidget::itemDoubleClicked, m_openBtn, &QPushButton::click);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_openBtn, &QPushButton::clicked, this, [this] {
    if (QListWidgetItem *item = m_list->currentItem()) {
      m_selected = TestProfilesStore::get(item->data(Qt::UserRole).toLongLong());
      if (m_selected) {
        accept();
      }
    }
  });
  connect(m_deleteBtn, &QPushButton::clicked, this, [this] {
    QListWidgetItem *item = m_list->currentItem();
    if (!item) {
      return;
    }
    qint64 const id{item->data(Qt::UserRole).toLongLong()};
    auto *confirm = new ConfDialog(tr("Delete"), tr("Delete '%1' from the library?").arg(item->data(Qt::UserRole + 1).toString()),
                                   tr("Delete"), tr("Cancel"), this);
    connect(confirm, &QDialog::accepted, this, [this, id] {
      TestProfilesStore::remove(id);
      refresh();
    });
    confirm->open();
  });

  refresh();
}

std::optional<TestProfile> ProgramLibraryDialog::selected() const {
  return m_selected;
}

void ProgramLibraryDialog::refresh() {
  m_list->clear();
  for (TestProfile const &profile : TestProfilesStore::list()) {
    auto *item = new QListWidgetItem(tr("%1   (%n interval(s), %2)", "", profile.steps.size())
                                       .arg(profile.name, ExperimentLibrary::formatDuration(profile.totalDurationS())),
                                     m_list);
    item->setData(Qt::UserRole, profile.id);
    item->setData(Qt::UserRole + 1, profile.name);
    item->setSizeHint(QSize(0, static_cast<int>(36 * utils::ssf()))); // tall enough to tap (a style sheet height overlaps rows)
  }
  if (m_list->count() > 0) {
    m_list->setCurrentRow(0);
  }
  showPreview();
}

void ProgramLibraryDialog::showPreview() {
  QListWidgetItem *item = m_list->currentItem();
  m_openBtn->setEnabled(item);
  m_deleteBtn->setEnabled(item);
  if (!item) {
    m_preview->setText(tr("The library is empty. Use \"Save to library…\" on the Program page."));
    return;
  }
  auto const profile = TestProfilesStore::get(item->data(Qt::UserRole).toLongLong());
  if (!profile) {
    return;
  }
  QStringList lines{QStringLiteral("<b>%1</b>").arg(profile->name.toHtmlEscaped()),
                    tr("by %1, changed %2").arg(profile->createdBy.toHtmlEscaped(), profile->updatedAt.left(16).replace('T', ' ')),
                    QString{}};
  for (int i{0}; i < profile->steps.size(); ++i) {
    TestProfileStep const &step = profile->steps[i];
    lines << QStringLiteral("%1. %2 · %3%4")
               .arg(i + 1)
               .arg(ExperimentLibrary::formatDuration(step.durationS), setpointsText(step).toHtmlEscaped(),
                    step.label.isEmpty() ? QString{} : QStringLiteral(" · <i>%1</i>").arg(step.label.toHtmlEscaped()));
  }
  if (profile->programJson.isEmpty()) {
    lines << QString{} << tr("Saved as steps only: each step holds its setpoint for its duration.");
  }
  m_preview->setText(lines.join("<br>"));
}

// ---------------------------------------------------------------- save

ProgramSaveDialog::ProgramSaveDialog(ProgramModel const &model, QString const &name, QStringList const &labels,
                                     QString const &userName, QWidget *parent)
    : QDialog{parent}, m_model{model}, m_userName{userName} {
  setWindowTitle(tr("Save to library"));
  fitToScreen(this, parent, 820, 560);

  auto *root    = new QVBoxLayout(this);
  auto *nameRow = new QHBoxLayout();
  m_nameEdit    = new LineEditScreen(this, VirtualKeyboard::EN);
  m_nameEdit->setText(name);
  m_nameEdit->setPlaceholderText(tr("e.g. Thermal cycle A"));
  nameRow->addWidget(new QLabel(tr("Name"), this));
  nameRow->addWidget(m_nameEdit, 1);
  root->addLayout(nameRow);

  root->addWidget(new QLabel(tr("Interval labels (optional, shown while the program runs):"), this));
  QVector<TestProfileStep> const steps = programlibrary::stepsFromModel(model, labels);
  m_table                              = new QTableWidget(static_cast<int>(steps.size()), 3, this);
  m_table->setHorizontalHeaderLabels({tr("Duration"), tr("Setpoints"), tr("Label")});
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  m_table->verticalHeader()->setDefaultSectionSize(static_cast<int>(44 * utils::ssf()));
  for (int row{0}; row < steps.size(); ++row) {
    m_table->setItem(row, 0, new QTableWidgetItem(ExperimentLibrary::formatDuration(steps[row].durationS)));
    m_table->setItem(row, 1, new QTableWidgetItem(setpointsText(steps[row])));
    for (int c : {0, 1}) {
      m_table->item(row, c)->setFlags(Qt::ItemIsEnabled);
    }
    auto *label = new LineEditScreen(nullptr, VirtualKeyboard::EN);
    label->setText(steps[row].label);
    m_table->setCellWidget(row, 2, label);
  }
  root->addWidget(m_table, 1);

  auto *buttons   = new QHBoxLayout();
  auto *saveBtn   = makeButton(tr("Save"), this);
  auto *cancelBtn = makeButton(tr("Cancel"), this);
  buttons->addWidget(saveBtn);
  buttons->addStretch(1);
  buttons->addWidget(cancelBtn);
  root->addLayout(buttons);

  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(saveBtn, &QPushButton::clicked, this, [this] {
    save(false);
  });
}

std::optional<TestProfile> ProgramSaveDialog::saved() const {
  return m_saved;
}

void ProgramSaveDialog::save(bool replaceExisting) {
  QString const name = m_nameEdit->text().trimmed();
  QStringList   labels;
  for (int row{0}; row < m_table->rowCount(); ++row) {
    labels << qobject_cast<QLineEdit *>(m_table->cellWidget(row, 2))->text().trimmed();
  }
  QVector<TestProfileStep> const steps = programlibrary::stepsFromModel(m_model, labels);
  QString const programJson = QString::fromUtf8(QJsonDocument::fromVariant(m_model.programVariant()).toJson(QJsonDocument::Compact));

  auto const existing = TestProfilesStore::findByName(name);
  if (existing && !replaceExisting) {
    auto *confirm = new ConfDialog(tr("Save to library"), tr("'%1' already exists. Replace it?").arg(name), tr("Replace"),
                                   tr("Cancel"), this);
    connect(confirm, &QDialog::accepted, this, [this] {
      save(true);
    });
    confirm->open();
    return;
  }

  QString error;
  m_saved = existing ? TestProfilesStore::update(existing->id, name, existing->description, steps, &error, programJson)
                     : TestProfilesStore::add(name, QString{}, steps, m_userName, &error, programJson);
  if (!m_saved) {
    (new MessageDialog(tr("Could not save the program"), error))->open();
    return;
  }
  accept();
}

}
