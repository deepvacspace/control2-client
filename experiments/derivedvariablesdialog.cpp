#include "derivedvariablesdialog.h"

#include <QScreen>
#include <algorithm>

#include <QGuiApplication>

#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "constants.h"
#include "derivedexpression.h"
#include "dialogs/messagedialog.h"
#include "lineeditnum.h"
#include "lineeditscreen.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {

using storage::DerivedVariable;
using storage::DerivedVariablesStore;

namespace {
QPushButton *makeButton(QString const &text, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setFixedHeight(theme::dp(UI_BTN_HEIGHT));
  button->setFocusPolicy(Qt::NoFocus);
  return button;
}

QComboBox *makeChannelCombo(QVector<ChannelInfo> const &channels, QWidget *parent) {
  auto *combo = new QComboBox(parent);
  combo->setMinimumHeight(theme::dp(UI_BTN_HEIGHT));
  for (ChannelInfo const &channel : channels) {
    combo->addItem(QStringLiteral("%1  (%2)").arg(channel.label, channel.name), channel.name);
  }
  return combo;
}

QString describe(DerivedVariable const &definition) {
  switch (definition.type) {
    case DerivedVariable::Type::Difference:
      return QStringLiteral("%1 − %2").arg(definition.sourceChannel, definition.sourceChannel2);
    case DerivedVariable::Type::RateOfChange:
      return QStringLiteral("d(%1)/dt").arg(definition.sourceChannel);
    case DerivedVariable::Type::RollingStd:
      return QObject::tr("std(%1, %2 samples)").arg(definition.sourceChannel).arg(definition.window);
    case DerivedVariable::Type::CumulativeIntegral:
      return QStringLiteral("∫ %1 dt").arg(definition.sourceChannel);
    case DerivedVariable::Type::Custom:
      return definition.expression;
  }
  return {};
}
}

DerivedVariablesDialog::DerivedVariablesDialog(QVector<ChannelInfo> const &channels, QString const &userName, QWidget *parent)
    : QDialog{parent}, m_channels{channels}, m_userName{userName}, m_color{QColor("#9333ea")} {
  setWindowTitle(tr("Derived variables"));
  // as large as wanted, but never beyond the screen it opens on
  QScreen const *screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
  QRect const    room   = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
  resize(std::min(theme::dp(1687), room.width() * 9 / 10),
         std::min(static_cast<int>(640 * utils::ssf()), room.height() * 9 / 10));
  setMaximumHeight(room.height() * 95 / 100);

  auto *root = new QVBoxLayout(this);

  auto *listRow = new QHBoxLayout();
  m_list        = new QListWidget(this);
  m_list->setMinimumHeight(theme::dp(150));
  m_list->setStyleSheet(QStringLiteral("QListWidget::item { height: %1px; }").arg(static_cast<int>(34 * utils::ssf())));
  auto *deleteBtn = makeButton(tr("Delete"), this);
  listRow->addWidget(m_list, 1);
  listRow->addWidget(deleteBtn, 0, Qt::AlignTop);
  root->addLayout(listRow);

  auto *form = new QGridLayout();
  int   row{0};
  auto  addRow = [&](QString const &label, QWidget *field, QList<QWidget *> *rowWidgets = nullptr) {
    auto *caption = new QLabel(label, this);
    form->addWidget(caption, row, 0);
    form->addWidget(field, row, 1);
    if (rowWidgets) {
      *rowWidgets = {caption, field};
    }
    ++row;
  };

  m_nameEdit = new LineEditScreen(this, VirtualKeyboard::EN);
  m_nameEdit->setPlaceholderText(tr("e.g. temperature_error"));
  addRow(tr("Name"), m_nameEdit);

  m_typeCombo = new QComboBox(this);
  m_typeCombo->setMinimumHeight(theme::dp(UI_BTN_HEIGHT));
  m_typeCombo->addItem(tr("Difference (A − B)"), static_cast<int>(DerivedVariable::Type::Difference));
  m_typeCombo->addItem(tr("Rate of change (dA/dt, per second)"), static_cast<int>(DerivedVariable::Type::RateOfChange));
  m_typeCombo->addItem(tr("Rolling standard deviation"), static_cast<int>(DerivedVariable::Type::RollingStd));
  m_typeCombo->addItem(tr("Cumulative integral (∫A dt)"), static_cast<int>(DerivedVariable::Type::CumulativeIntegral));
  m_typeCombo->addItem(tr("Custom expression"), static_cast<int>(DerivedVariable::Type::Custom));
  addRow(tr("Type"), m_typeCombo);

  m_sourceCombo = makeChannelCombo(m_channels, this);
  addRow(tr("Channel A"), m_sourceCombo, &m_sourceRow);
  m_source2Combo = makeChannelCombo(m_channels, this);
  addRow(tr("Channel B"), m_source2Combo, &m_source2Row);

  m_windowEdit = new LineEditNum(2, 100000, 0, false, this);
  m_windowEdit->setValue(10);
  m_windowEdit->setFixedWidth(theme::dp(262));
  addRow(tr("Window, samples"), m_windowEdit, &m_windowRow);

  // expression + a picker that inserts exact channel names
  auto *expressionBox = new QWidget(this);
  auto *expressionLay = new QVBoxLayout(expressionBox);
  expressionLay->setContentsMargins(0, 0, 0, 0);
  m_expressionEdit = new LineEditScreen(expressionBox, VirtualKeyboard::EN);
  m_expressionEdit->setPlaceholderText(tr("e.g. temp_u_p + temp_u_i + temp_u_d"));
  auto *insertRow = new QHBoxLayout();
  m_insertCombo   = makeChannelCombo(m_channels, expressionBox);
  auto *insertBtn = makeButton(tr("Insert channel"), expressionBox);
  insertRow->addWidget(m_insertCombo, 1);
  insertRow->addWidget(insertBtn);
  auto *functions = new QLabel(tr("Operators + − * / // % ** ( ); functions abs min max sqrt log log10 exp sin cos tan "
                                  "clip(x, lo, hi)"),
                               expressionBox);
  functions->setWordWrap(true);
  functions->setStyleSheet("color: #5c6472;");
  expressionLay->addWidget(m_expressionEdit);
  expressionLay->addLayout(insertRow);
  expressionLay->addWidget(functions);
  addRow(tr("Expression"), expressionBox, &m_expressionRow);
  connect(insertBtn, &QPushButton::clicked, this, [this] {
    QString text = m_expressionEdit->text();
    if (!text.isEmpty() && !text.endsWith(' ') && !text.endsWith('(')) {
      text += ' ';
    }
    m_expressionEdit->setText(text + m_insertCombo->currentData().toString());
  });

  m_colorBtn = makeButton(QString{}, this);
  m_colorBtn->setFixedWidth(theme::dp(150));
  addRow(tr("Color"), m_colorBtn);
  auto updateSwatch = [this] {
    m_colorBtn->setStyleSheet(QStringLiteral("background: %1;").arg(m_color.name()));
  };
  updateSwatch();
  connect(m_colorBtn, &QPushButton::clicked, this, [this, updateSwatch] {
    QColor const picked = QColorDialog::getColor(m_color, this, tr("Color"));
    if (picked.isValid()) {
      m_color = picked;
      updateSwatch();
    }
  });
  form->setColumnStretch(1, 1);
  root->addLayout(form);
  root->addStretch(1);

  auto *buttons  = new QHBoxLayout();
  auto *addBtn   = makeButton(tr("Add"), this);
  auto *closeBtn = makeButton(tr("Close"), this);
  buttons->addWidget(addBtn);
  buttons->addStretch(1);
  buttons->addWidget(closeBtn);
  root->addLayout(buttons);

  connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &DerivedVariablesDialog::updateForm);
  connect(addBtn, &QPushButton::clicked, this, &DerivedVariablesDialog::add);
  connect(deleteBtn, &QPushButton::clicked, this, &DerivedVariablesDialog::deleteSelected);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

  refreshList();
  updateForm();
}

void DerivedVariablesDialog::refreshList() {
  m_list->clear();
  QStringList names;
  for (ChannelInfo const &channel : std::as_const(m_channels)) {
    names.push_back(channel.name);
  }
  for (DerivedVariable const &definition : DerivedVariablesStore::list()) {
    QStringList missing;
    for (QString const &name : derived::requiredChannels(definition)) {
      if (!names.contains(name)) {
        missing.push_back(name);
      }
    }
    QPixmap swatch(14, 14);
    swatch.fill(definition.color);
    QString text = QStringLiteral("%1  =  %2").arg(definition.name, describe(definition));
    if (!missing.isEmpty()) {
      text += tr("   (not available here: no %1)").arg(missing.join(", "));
    }
    auto *item = new QListWidgetItem(QIcon(swatch), text, m_list);
    item->setData(Qt::UserRole, definition.id);
  }
}

void DerivedVariablesDialog::updateForm() {
  auto const type = static_cast<DerivedVariable::Type>(m_typeCombo->currentData().toInt());
  auto show = [](QList<QWidget *> const &widgets, bool visible) {
    for (QWidget *widget : widgets) {
      widget->setVisible(visible);
    }
  };
  show(m_sourceRow, type != DerivedVariable::Type::Custom);
  show(m_source2Row, type == DerivedVariable::Type::Difference);
  show(m_windowRow, type == DerivedVariable::Type::RollingStd);
  show(m_expressionRow, type == DerivedVariable::Type::Custom);
}

void DerivedVariablesDialog::add() {
  DerivedVariable definition;
  definition.name           = m_nameEdit->text().trimmed();
  definition.type           = static_cast<DerivedVariable::Type>(m_typeCombo->currentData().toInt());
  definition.sourceChannel  = m_sourceCombo->currentData().toString();
  definition.sourceChannel2 = m_source2Combo->currentData().toString();
  definition.window         = m_windowEdit->text().toInt();
  definition.expression     = m_expressionEdit->text().trimmed();
  definition.color          = m_color;
  definition.createdBy      = m_userName;

  // a name that collides with a real channel would hide it
  for (ChannelInfo const &channel : std::as_const(m_channels)) {
    if (channel.name == definition.name) {
      (new MessageDialog(tr("Derived variables"), tr("'%1' is already a channel name.").arg(definition.name)))->open();
      return;
    }
  }

  QString error;
  if (!DerivedVariablesStore::add(definition, &error)) {
    (new MessageDialog(tr("Could not add the derived variable"), error))->open();
    return;
  }
  m_changed = true;
  m_nameEdit->clear();
  m_expressionEdit->clear();
  refreshList();
}

void DerivedVariablesDialog::deleteSelected() {
  if (QListWidgetItem *item = m_list->currentItem()) {
    DerivedVariablesStore::remove(item->data(Qt::UserRole).toLongLong());
    m_changed = true;
    refreshList();
  }
}

}
