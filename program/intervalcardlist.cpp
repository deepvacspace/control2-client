#include "intervalcardlist.h"

#include <QCheckBox>
#include <QComboBox>
#include <algorithm>
#include <cmath>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItem>
#include <QVBoxLayout>

#include "constants.h"
#include "lineeditnum.h"
#include "lineeditscreen.h"
#include "lineedittime.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
QString number(double value, int prec) {
  return QString::number(value, 'f', prec).replace(QLatin1Char('-'), QChar(0x2212));
}

// a custom ramp's rate (°C/min), on its flags item: kept while the program is edited, so its duration follows its change
// of temperature. Not saved: a loaded program's rate is worked out from its ramp (change and duration).
constexpr int RATE_ROLE{Qt::UserRole + 20};
// the custom ramp rates allowed
constexpr double RATE_MIN{0.01};
constexpr double RATE_MAX{10.0};

// the temperature's mode (the combo's item data)
enum Mode { FULL_SPEED = 0, LINEAR_RAMP = 1, CUSTOM_RATE = 2 };
}

IntervalCardList::IntervalCardList(QWidget *parent) : QWidget{parent} {
  m_lay = new QVBoxLayout(this);
  m_lay->setContentsMargins(0, 0, theme::dp(16), 0);
  m_lay->setSpacing(theme::dp(12));
  m_lay->addStretch();
  // as tall as its cards: what is under it (the cycle) follows a closed card right away
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  styleCards();
}

void IntervalCardList::setModel(ProgramModel *model) {
  if (m_model) {
    disconnect(m_model, nullptr, this, nullptr);
    disconnect(m_model->getStandardModel(), nullptr, this, nullptr);
  }
  m_model = model;
  if (m_model) {
    connect(m_model, &ProgramModel::numberOfIntervalChanged, this, &IntervalCardList::rebuild);
    auto changed = [this] {
      if (!m_committing) {
        if (applyCustomRates()) { // changed elsewhere (the interval dialog): custom ramps keep their rates
          commit();
        } else {
          refreshTexts();
          fillEditor();
        }
      }
    };
    connect(m_model, &ProgramModel::modelChanged, this, changed);
    connect(m_model->getStandardModel(), &QStandardItemModel::dataChanged, this, changed);
    connect(m_model, &ProgramModel::intervalSelected, this, &IntervalCardList::openCard);
  }
  rebuild();
}

void IntervalCardList::setLabels(QStringList const &labels) {
  m_labels = labels;
  refreshTexts();
  if (m_labelEdit && !m_labelEdit->hasFocus() && (m_labelEdit->text() != m_labels.value(m_open))) {
    m_labelEdit->setText(m_labels.value(m_open));
  }
}

void IntervalCardList::setNotes(QStringList const &notes) {
  if (notes == m_notes) {
    return;
  }
  m_notes = notes;
  refreshTexts();
}

void IntervalCardList::setEditable(bool editable) {
  m_editable = editable;
  fillEditor();
}

void IntervalCardList::setPressureLabel(QString const &label) {
  m_pressureLabel = label;
  fillEditor();
}

void IntervalCardList::setStartTemperature(std::optional<double> temperature) {
  // shown only (a custom ramp's time changes when the program is edited or started, not as the chamber's temperature
  // moves); small moves are not worth a refresh
  if ((temperature.has_value() == m_startTemp.has_value()) && (!temperature || (std::abs(*temperature - *m_startTemp) < 0.1))) {
    return;
  }
  m_startTemp = temperature;
  refreshTexts();
  fillEditor();
}

void IntervalCardList::showInterval(int interval) {
  openCard(interval);
}

void IntervalCardList::makeHold(int interval, int seconds) {
  int const durationRow{rowOf(QStringLiteral("duration"))};
  QStandardItem *item = flagsItem(interval);
  if (!item || (durationRow < 0)) {
    return;
  }
  quint32 flags{flagsOf(interval)};
  if (int const tempRow{rowOf(QStringLiteral("temp"))}; tempRow >= 0) {
    utils::setBitOfDword(flags, tempRow, true);
  }
  utils::setBitOfDword(flags, RAMP_BIT, false);
  utils::setBitOfDword(flags, CUSTOM_RATE_BIT, false);
  utils::setBitOfDword(flags, AWAIT_BIT, true);
  m_committing = true;
  setFlags(interval, flags);
  item->setData(QVariant{}, RATE_ROLE);
  m_model->getStandardModel()->item(durationRow, interval)
    ->setData(seconds, m_model->getParamListVariant().value(durationRow).toMap().value("type").toInt());
  m_committing = false;
  commit();
}

void IntervalCardList::prepareForStart() {
  if (applyCustomRates()) {
    refreshTexts();
    fillEditor();
  }
}

QString IntervalCardList::pressureUnit() const {
  int const comma{static_cast<int>(m_pressureLabel.lastIndexOf(QLatin1Char(',')))};
  return (comma > 0) ? m_pressureLabel.mid(comma + 1).trimmed() : QStringLiteral("Torr");
}

int IntervalCardList::rowOf(QString const &name) const {
  return m_model ? static_cast<int>(m_model->getParamListOrder().indexOf(name)) : -1;
}

double IntervalCardList::valueOf(int row, int interval) const {
  int const type{m_model->getParamListVariant().value(row).toMap().value("type").toInt()};
  return m_model->getStandardModel()->item(row, interval)->data(type).toDouble();
}

// one of them: the temperature unless only the pressure is controlled (or the chamber has no temperature)
int IntervalCardList::variableRow(quint32 flags) const {
  int const tempRow{rowOf(QStringLiteral("temp"))};
  int const presRow{rowOf(QStringLiteral("pres"))};
  if (tempRow < 0) {
    return presRow;
  }
  bool const onlyPressure{(presRow >= 0) && utils::getBitOfDword(flags, presRow) && !utils::getBitOfDword(flags, tempRow)};
  return onlyPressure ? presRow : tempRow;
}

bool IntervalCardList::isPressure(quint32 flags) const {
  int const row{variableRow(flags)};
  return (row >= 0) && (row == rowOf(QStringLiteral("pres")));
}

// as the PLC runs it: a ramp bit is a ramp (neither bit: the setpoint at once, shown as a linear ramp as before)
int IntervalCardList::modeOf(quint32 flags) {
  if (utils::getBitOfDword(flags, RAMP_BIT)) {
    return utils::getBitOfDword(flags, CUSTOM_RATE_BIT) ? CUSTOM_RATE : LINEAR_RAMP;
  }
  return utils::getBitOfDword(flags, AWAIT_BIT) ? FULL_SPEED : LINEAR_RAMP;
}

// a ramp starts from the previous interval's target; the first one from the chamber's temperature (when known)
std::optional<double> IntervalCardList::rampChange(int interval) const {
  int const row{rowOf(QStringLiteral("temp"))};
  if (row < 0) {
    return std::nullopt;
  }
  if (interval < 1) {
    return m_startTemp ? std::optional<double>{std::abs(valueOf(row, 0) - *m_startTemp)} : std::nullopt;
  }
  return std::abs(valueOf(row, interval) - valueOf(row, interval - 1));
}

std::optional<double> IntervalCardList::rampRate(int interval) const {
  if (modeOf(flagsOf(interval)) == CUSTOM_RATE) {
    if (QVariant const rate = flagsItem(interval) ? flagsItem(interval)->data(RATE_ROLE) : QVariant{}; rate.isValid()) {
      return rate.toDouble();
    }
  }
  std::optional<double> const change = rampChange(interval);
  int const                   durationRow{rowOf(QStringLiteral("duration"))};
  if (!change || (durationRow < 0)) {
    return std::nullopt;
  }
  double const duration{valueOf(durationRow, interval)};
  return (duration > 0) ? std::optional<double>{*change / duration * 60.0} : std::nullopt;
}

// a custom ramp keeps its rate: its duration follows its change of temperature (its target or the previous one edited,
// intervals added or removed)
bool IntervalCardList::applyCustomRates() {
  int const durationRow{rowOf(QStringLiteral("duration"))};
  if (!m_model || (durationRow < 0)) {
    return false;
  }
  int const type{m_model->getParamListVariant().value(durationRow).toMap().value("type").toInt()};
  bool      changed{false};
  bool const committing{m_committing};
  m_committing = true; // the rates stored below are no edit to react to
  for (int interval{0}; interval < m_model->getStandardModel()->columnCount(); ++interval) {
    QStandardItem *item = flagsItem(interval);
    std::optional<double> const change = rampChange(interval);
    if (!item || (modeOf(flagsOf(interval)) != CUSTOM_RATE) || !change) {
      continue;
    }
    QVariant const rate = item->data(RATE_ROLE);
    if (!rate.isValid()) { // loaded: the rate of the ramp as it is
      double const duration{valueOf(durationRow, interval)};
      if ((duration > 0) && (*change > 0)) {
        item->setData(*change / duration * 60.0, RATE_ROLE);
      }
      continue;
    }
    // at least a second, and below the time that means "held until stopped" (a slow ramp over a large change)
    int const duration{static_cast<int>(
      std::clamp(std::round(*change / std::max(rate.toDouble(), RATE_MIN) * 60.0), 1.0, static_cast<double>(HOLD_UNTIL_STOPPED - 1)))};
    if (duration != static_cast<int>(valueOf(durationRow, interval))) {
      m_model->getStandardModel()->item(durationRow, interval)->setData(duration, type);
      changed = true;
    }
  }
  m_committing = committing;
  return changed;
}

quint32 IntervalCardList::flagsOf(int interval) const {
  int const            row{m_model ? m_model->getFlagsIndex() : -1};
  QStandardItem const *item = (row >= 0) ? m_model->getStandardModel()->item(row, interval) : nullptr;
  return item ? item->data(Qt::UserRole + 1).toUInt() : 0U;
}

void IntervalCardList::setFlags(int interval, quint32 flags) {
  if (QStandardItem *item = flagsItem(interval)) {
    item->setData(flags, Qt::UserRole + 1);
  }
}

QStandardItem *IntervalCardList::flagsItem(int interval) const {
  int const row{m_model ? m_model->getFlagsIndex() : -1};
  return (row >= 0) ? m_model->getStandardModel()->item(row, interval) : nullptr;
}

void IntervalCardList::commit() {
  m_committing = true;
  applyCustomRates();
  m_model->emitModelUpdated();
  m_committing = false;
  refreshTexts();
  fillEditor(); // the calculated fields (ramp rate or duration) and the fields that depend on the mode
}

void IntervalCardList::closeEditor() {
  if (m_editor) {
    m_editor->hide();
    m_editor->deleteLater();
  }
  m_editor = nullptr;
  m_valueEdits.clear();
  m_variableCombo = nullptr;
  m_setpointLabel = nullptr;
  m_durationEdit  = nullptr;
  m_durationLabel = nullptr;
  m_untilStoppedCheck = nullptr;
  m_modeCombo     = nullptr;
  m_rateEdit      = nullptr;
  m_labelEdit     = nullptr;
  m_duplicateBtn = nullptr;
  m_holdBtn      = nullptr;
  m_deleteBtn    = nullptr;
}

void IntervalCardList::rebuild() {
  closeEditor(); // it goes with its card
  for (Card const &card : std::as_const(m_cards)) {
    card.frame->deleteLater();
  }
  m_cards.clear();
  m_open = -1;
  if (!m_model) {
    return;
  }
  // an interval added or removed changes the ramp of the one after it: a custom ramp keeps its rate
  if (applyCustomRates()) {
    m_committing = true;
    m_model->emitModelUpdated();
    m_committing = false;
  }
  int const count{m_model->getStandardModel()->columnCount()};
  for (int interval{0}; interval < count; ++interval) {
    Card card;
    card.frame = new QFrame(this);
    card.frame->setObjectName("intervalCard");
    auto *cardLay = new QVBoxLayout(card.frame);
    cardLay->setContentsMargins(theme::dp(16), theme::dp(12), theme::dp(16), theme::dp(12));
    cardLay->setSpacing(theme::dp(12));
    // the head: a tap opens or closes the card
    card.head = new QFrame(card.frame);
    card.head->setProperty("interval", interval);
    card.head->installEventFilter(this);
    card.head->setCursor(Qt::PointingHandCursor);
    auto *headLay = new QHBoxLayout(card.head);
    headLay->setContentsMargins(0, 0, 0, 0);
    headLay->setSpacing(theme::dp(16));
    card.number = new QLabel(QStringLiteral("%1").arg(interval + 1, 2, 10, QLatin1Char('0')), card.head);
    card.number->setObjectName("intervalNumber");
    card.number->setAlignment(Qt::AlignCenter);
    card.number->setFixedSize(theme::dp(52), theme::dp(41));
    auto *texts = new QVBoxLayout();
    texts->setSpacing(0);
    card.title = new QLabel(card.head);
    QFont bold = card.title->font();
    bold.setBold(true);
    card.title->setFont(bold);
    card.subtitle = new QLabel(card.head);
    card.subtitle->setProperty("muted", true);
    QFont small = card.subtitle->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    card.subtitle->setFont(small);
    texts->addWidget(card.title);
    texts->addWidget(card.subtitle);
    card.chevron = new QLabel(card.head);
    headLay->addWidget(card.number, 0, Qt::AlignTop);
    headLay->addLayout(texts, 1);
    headLay->addWidget(card.chevron);
    cardLay->addWidget(card.head);
    m_lay->insertWidget(m_lay->count() - 1, card.frame); // before the stretch
    m_cards.push_back(card);
  }
  refreshTexts();
  openCard(qBound(0, m_model->getSelectedInterval(), count - 1));
}

void IntervalCardList::refreshTexts() {
  if (!m_model || (m_cards.size() != m_model->getStandardModel()->columnCount())) {
    return;
  }
  QStringList const  order  = m_model->getParamListOrder();
  QVariantList const params = m_model->getParamListVariant();
  int const          durationRow{static_cast<int>(order.indexOf(QStringLiteral("duration")))};
  int const          tempRow{static_cast<int>(order.indexOf(QStringLiteral("temp")))};
  int const          flagsRow{m_model->getFlagsIndex()};
  int const          chevron{theme::dp(26)};
  theme::Colors const &c = theme::colors();
  for (int interval{0}; interval < m_cards.size(); ++interval) {
    Card const   &card = m_cards[interval];
    quint32 const flags{flagsOf(interval)};
    auto value = [&](int row) {
      return m_model->getStandardModel()->item(row, interval)->data(params.value(row).toMap().value("type").toInt()).toDouble();
    };
    // what the interval does
    QString title;
    if ((tempRow >= 0) && utils::getBitOfDword(flags, tempRow)) {
      QString const               target = number(value(tempRow), params.value(tempRow).toMap().value("prec", 1).toInt());
      std::optional<double> const rate   = rampRate(interval);
      title = (modeOf(flags) == CUSTOM_RATE) && rate ? tr("Ramp to %1 °C at %2 °C/min").arg(target, number(*rate, 2))
              : utils::getBitOfDword(flags, RAMP_BIT)  ? tr("Ramp to %1 °C").arg(target)
              : utils::getBitOfDword(flags, AWAIT_BIT) ? tr("Go to %1 °C and hold").arg(target)
                                                       : tr("Go to %1 °C").arg(target);
    } else {
      QStringList others;
      for (int row{0}; row < order.size(); ++row) {
        if ((row != durationRow) && (row != flagsRow) && (row != tempRow) && utils::getBitOfDword(flags, row)) {
          QVariantMap const param = params.value(row).toMap();
          bool const        scientific{param.value("notation").toString() == QLatin1String("scientific")};
          others << QStringLiteral("%1 %2").arg(param.value("label").toString(),
                                                scientific ? utils::formatSci10(value(row)) : number(value(row), param.value("prec", 1).toInt()));
        }
      }
      title = others.isEmpty() ? tr("No setpoint") : others.join(QStringLiteral(" · "));
    }
    card.title->setText(title);
    // how long, and its label
    long const  duration{(durationRow >= 0) ? static_cast<long>(value(durationRow)) : 0L};
    QStringList parts{(duration >= HOLD_UNTIL_STOPPED) ? tr("until stopped") : utils::timeFromSec(duration)};
    if (QString const note = m_notes.value(interval); !note.isEmpty()) {
      parts << note;
    }
    if (QString const label = m_labels.value(interval); !label.isEmpty()) {
      parts << label;
    }
    card.subtitle->setText(parts.join(QStringLiteral("  ·  ")));
    card.chevron->setPixmap(theme::menuIcon(interval == m_open ? QStringLiteral("up") : QStringLiteral("down"), c.muted, c.muted, chevron)
                              .pixmap(chevron));
  }
}

void IntervalCardList::openCard(int interval) {
  if ((interval == m_open) || (interval < 0) || (interval >= m_cards.size())) {
    return;
  }
  closeEditor();
  m_open = interval;
  buildEditor(interval);
  if (m_model) {
    m_model->selectInterval(interval); // the graph shows it too
  }
  styleCards();
  refreshTexts();
  // in sight in the drawer
  for (QWidget *parent = parentWidget(); parent; parent = parent->parentWidget()) {
    if (auto *area = qobject_cast<QScrollArea *>(parent)) {
      QFrame *frame = m_cards[interval].frame;
      QMetaObject::invokeMethod(
        area,
        [area, frame] {
          // up or down only: never sideways (that cut the cards' left edge while the drawer was still being laid out)
          area->horizontalScrollBar()->setValue(0);
          if (!area->widget() || !area->widget()->isAncestorOf(frame)) {
            return;
          }
          int const      margin{theme::dp(15)};
          int const      top{frame->mapTo(area->widget(), QPoint(0, 0)).y()};
          int const      bottom{top + frame->height()};
          QScrollBar    *bar = area->verticalScrollBar();
          int const      view{area->viewport()->height()};
          if (top - margin < bar->value()) {
            bar->setValue(top - margin);
          } else if (bottom + margin > bar->value() + view) {
            bar->setValue(std::min(top - margin, bottom + margin - view));
          }
        },
        Qt::QueuedConnection);
      break;
    }
  }
}

void IntervalCardList::buildEditor(int interval) {
  Card const &card = m_cards[interval];
  m_editor         = new QWidget(card.frame);
  auto *lay        = new QVBoxLayout(m_editor);
  lay->setContentsMargins(0, theme::dp(4), 0, theme::dp(4));
  lay->setSpacing(theme::dp(16));
  auto *grid = new QGridLayout();
  grid->setHorizontalSpacing(theme::dp(16));
  grid->setVerticalSpacing(theme::dp(4));
  int  field{0};
  // in the field's cell (two columns, each title over its field); several edits may share one cell, one of them shown
  auto place = [&](QWidget *edit, int at) {
    grid->addWidget(edit, (at / 2) * 2 + 1, at % 2);
    edit->setMinimumHeight(theme::dp(60));
    edit->setMinimumWidth(0);
    edit->setMaximumWidth(QWIDGETSIZE_MAX);
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    edit->setFont(font()); // the page's font, not the large one of the screen inputs
    for (QWidget *part : edit->findChildren<QWidget *>()) {
      part->setFont(font());
    }
  };
  auto addField = [&](QString const &title, QWidget *edit) {
    auto *label = new QLabel(title, m_editor);
    label->setProperty("muted", true);
    grid->addWidget(label, (field / 2) * 2, field % 2);
    place(edit, field);
    ++field;
    return label;
  };

  QStringList const  order  = m_model->getParamListOrder();
  QVariantList const params = m_model->getParamListVariant();
  m_valueEdits.fill(nullptr, order.size());
  int const tempRow{rowOf(QStringLiteral("temp"))};
  int const presRow{rowOf(QStringLiteral("pres"))};

  // label
  m_labelEdit = new LineEditScreen(m_editor, VirtualKeyboard::EN);
  m_labelEdit->setPlaceholderText(tr("Optional"));
  addField(tr("Label"), m_labelEdit);
  // kept as it is typed: the on-screen keyboard sets the text at once (no editingFinished), and closing the card
  // must not lose it
  connect(m_labelEdit, &QLineEdit::textChanged, this, [this](QString const &text) {
    if ((m_open >= 0) && (text.trimmed() != m_labels.value(m_open))) {
      emit labelEdited(m_open, text.trimmed());
    }
  });

  // variable: temperature or pressure (the PLC ramps and holds temperature only)
  m_variableCombo = new QComboBox(m_editor);
  for (int row : {tempRow, presRow}) {
    if (row >= 0) {
      m_variableCombo->addItem(params.value(row).toMap().value("label").toString(), row);
    }
  }
  addField(tr("Variable"), m_variableCombo);
  connect(m_variableCombo, &QComboBox::activated, this, [this, tempRow, presRow](int index) {
    if ((m_open < 0) || !m_model) {
      return;
    }
    int const row{m_variableCombo->itemData(index).toInt()};
    quint32   flags{flagsOf(m_open)};
    for (int variable : {tempRow, presRow}) {
      if (variable >= 0) {
        utils::setBitOfDword(flags, variable, variable == row);
      }
    }
    if (row == presRow) { // no ramp or hold for pressure (a hold would wait for the temperature)
      utils::setBitOfDword(flags, RAMP_BIT, false);
      utils::setBitOfDword(flags, AWAIT_BIT, false);
      utils::setBitOfDword(flags, CUSTOM_RATE_BIT, false);
    } else if (!utils::getBitOfDword(flags, RAMP_BIT) && !utils::getBitOfDword(flags, AWAIT_BIT)) {
      utils::setBitOfDword(flags, RAMP_BIT, true);
    }
    m_committing = true;
    setFlags(m_open, flags);
    endUntilStopped(m_open);
    m_committing = false;
    commit();
  });

  // a number of the interval (setpoint), stored as it is given
  auto valueEdit = [&](int row) {
    QVariantMap const param = params.value(row).toMap();
    auto *edit = new LineEditNum(param.value("min").toDouble(), param.value("max").toDouble(), param.value("prec", 1).toInt(), true, m_editor);
    edit->setIsScientific(param.value("notation").toString() == QLatin1String("scientific"));
    m_valueEdits[row] = edit;
    connect(edit, &LineEditNum::userEditingFinished, this, [this, row, edit, tempRow, type = param.value("type").toInt()] {
      if ((m_open < 0) || !m_model) {
        return;
      }
      bool         ok{false};
      double const value = edit->number(&ok);
      if (!ok) {
        fillEditor();
        return;
      }
      m_committing = true;
      m_model->getStandardModel()->item(row, m_open)->setData(value, type);
      quint32 flags{flagsOf(m_open)};
      utils::setBitOfDword(flags, row, true); // given a value: used
      if ((row == tempRow) && !utils::getBitOfDword(flags, RAMP_BIT) && !utils::getBitOfDword(flags, AWAIT_BIT)) {
        utils::setBitOfDword(flags, RAMP_BIT, true);
      }
      setFlags(m_open, flags);
      m_committing = false;
      commit(); // a custom ramp (this one or the next) keeps its rate: its duration follows
    });
    return edit;
  };

  // setpoint of the variable: one edit each, in the same cell, the variable's shown
  int const setpointField{field};
  for (int row : {tempRow, presRow}) {
    if ((row < 0) || (params.value(row).toMap().value("type").toInt() != 0)) {
      continue;
    }
    LineEditNum *edit = valueEdit(row);
    if (!m_setpointLabel) {
      m_setpointLabel = addField(tr("Setpoint"), edit);
    } else {
      place(edit, setpointField);
    }
  }

  // mode: how the temperature changes
  m_modeCombo = new QComboBox(m_editor);
  m_modeCombo->addItem(tr("Full speed, then hold"), FULL_SPEED);
  m_modeCombo->addItem(tr("Linear ramp"), LINEAR_RAMP);
  m_modeCombo->addItem(tr("Custom ramp rate"), CUSTOM_RATE);
  m_modeCombo->setPlaceholderText(QStringLiteral("—")); // pressure: no mode
  addField(tr("Mode"), m_modeCombo);
  connect(m_modeCombo, &QComboBox::activated, this, [this](int index) {
    if ((m_open < 0) || !m_model) {
      return;
    }
    int const mode{m_modeCombo->itemData(index).toInt()};
    quint32   flags{flagsOf(m_open)};
    // a custom rate starts as the ramp's rate now
    std::optional<double> const rate = (modeOf(flags) == FULL_SPEED) ? std::nullopt : rampRate(m_open);
    utils::setBitOfDword(flags, RAMP_BIT, mode != FULL_SPEED);
    utils::setBitOfDword(flags, AWAIT_BIT, mode == FULL_SPEED);
    utils::setBitOfDword(flags, CUSTOM_RATE_BIT, mode == CUSTOM_RATE);
    m_committing = true;
    setFlags(m_open, flags);
    flagsItem(m_open)->setData((mode == CUSTOM_RATE) ? QVariant{qBound(RATE_MIN, rate.value_or(1.0), RATE_MAX)} : QVariant{}, RATE_ROLE);
    endUntilStopped(m_open);
    m_committing = false;
    commit();
  });

  // duration: the hold (full speed) or the ramp's (calculated for a custom rate); a hold, or a pressure, may last until
  // stopped (under the fields)
  if (int const durationRow{rowOf(QStringLiteral("duration"))}; durationRow >= 0) {
    m_durationEdit      = new LineEditTime(true, m_editor);
    m_durationLabel     = addField(tr("Duration"), m_durationEdit);
    m_untilStoppedCheck = new QCheckBox(tr("Hold until stopped"), m_editor);
    m_untilStoppedCheck->setFocusPolicy(Qt::NoFocus);
    m_untilStoppedCheck->setToolTip(tr("No hold time: held until Stop, or until Start replaces it"));
    int const type{params.value(durationRow).toMap().value("type").toInt()};
    connect(m_durationEdit, &LineEditTime::userEdited, this, [this, durationRow, type] {
      if ((m_open < 0) || !m_model) {
        return;
      }
      m_committing = true;
      m_model->getStandardModel()->item(durationRow, m_open)->setData(static_cast<int>(m_durationEdit->getSeconds()), type);
      m_committing = false;
      commit();
    });
    connect(m_untilStoppedCheck, &QCheckBox::clicked, this, [this, durationRow, type](bool checked) {
      if ((m_open < 0) || !m_model) {
        return;
      }
      m_committing = true;
      m_model->getStandardModel()->item(durationRow, m_open)->setData(static_cast<int>(checked ? HOLD_UNTIL_STOPPED : 3600U), type);
      m_committing = false;
      commit();
    });
  }

  // ramp rate: none at full speed, calculated for a linear ramp, given for a custom one
  m_rateEdit = new LineEditNum(RATE_MIN, RATE_MAX, 2, true, m_editor);
  addField(tr("Ramp rate, °C/min"), m_rateEdit);
  connect(m_rateEdit, &LineEditNum::userEditingFinished, this, [this] {
    if ((m_open < 0) || !m_model) {
      return;
    }
    bool         ok{false};
    double const rate = m_rateEdit->number(&ok);
    if (!ok || (rate <= 0)) {
      fillEditor();
      return;
    }
    m_committing = true;
    flagsItem(m_open)->setData(qBound(RATE_MIN, rate, RATE_MAX), RATE_ROLE);
    m_committing = false;
    commit(); // the duration follows
  });

  // other quantities of this chamber, if any
  for (int row{0}; row < order.size(); ++row) {
    if ((row != tempRow) && (row != presRow) && (params.value(row).toMap().value("type").toInt() == 0)) {
      addField(params.value(row).toMap().value("label").toString(), valueEdit(row));
    }
  }
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 1);
  lay->addLayout(grid);
  if (m_untilStoppedCheck) {
    lay->addWidget(m_untilStoppedCheck);
  }

  // Duplicate · Then hold · Delete
  auto *actions  = new QHBoxLayout();
  m_duplicateBtn = new QPushButton(tr("Duplicate"), m_editor);
  m_duplicateBtn->setToolTip(tr("A copy of this interval right after it"));
  m_holdBtn = new QPushButton(tr("Then hold"), m_editor);
  m_holdBtn->setToolTip(tr("Right after this ramp: an interval that holds its target (full speed, then hold)"));
  m_deleteBtn = new QPushButton(tr("Delete"), m_editor);
  m_deleteBtn->setProperty("outline", true);
  for (QPushButton *button : {m_duplicateBtn, m_holdBtn, m_deleteBtn}) {
    button->setFixedHeight(theme::dp(52));
    button->setFocusPolicy(Qt::NoFocus);
  }
  actions->addWidget(m_duplicateBtn);
  actions->addWidget(m_holdBtn);
  actions->addStretch();
  connect(m_holdBtn, &QPushButton::clicked, this, [this] {
    if (m_open >= 0) {
      emit holdRequested(m_open);
    }
  });
  actions->addWidget(m_deleteBtn);
  lay->addLayout(actions);
  connect(m_duplicateBtn, &QPushButton::clicked, this, [this] {
    if (m_open >= 0) {
      emit duplicateRequested(m_open);
    }
  });
  connect(m_deleteBtn, &QPushButton::clicked, this, [this] {
    if (m_open >= 0) {
      emit deleteRequested(m_open);
    }
  });

  card.frame->layout()->addWidget(m_editor);
  fillEditor();
  styleCards(); // the button icons in the theme's colours
}

void IntervalCardList::endUntilStopped(int interval) {
  int const     durationRow{rowOf(QStringLiteral("duration"))};
  quint32 const flags{flagsOf(interval)};
  if ((durationRow < 0) || isPressure(flags) || (modeOf(flags) == FULL_SPEED) || (valueOf(durationRow, interval) < HOLD_UNTIL_STOPPED)) {
    return;
  }
  m_model->getStandardModel()->item(durationRow, interval)->setData(3600, m_model->getParamListVariant().value(durationRow).toMap().value("type").toInt());
}

void IntervalCardList::fillEditor() {
  if (!m_editor || !m_model || (m_open < 0) || (m_open >= m_model->getStandardModel()->columnCount())) {
    return;
  }
  quint32 const flags{flagsOf(m_open)};
  int const     tempRow{rowOf(QStringLiteral("temp"))};
  int const     presRow{rowOf(QStringLiteral("pres"))};
  int const     variable{variableRow(flags)};
  bool const    pressure{isPressure(flags)};
  int const     mode{modeOf(flags)};
  // a custom rate needs where the ramp starts: the first interval's, without the chamber's temperature, is edited as a
  // linear one
  bool const    rampKnown{rampChange(m_open).has_value()};
  bool const    customRate{(mode == CUSTOM_RATE) && rampKnown};

  if (!m_labelEdit->hasFocus() && (m_labelEdit->text() != m_labels.value(m_open))) {
    m_labelEdit->setText(m_labels.value(m_open));
  }
  m_labelEdit->setEnabled(m_editable);

  m_variableCombo->setCurrentIndex(m_variableCombo->findData(variable));
  m_variableCombo->setEnabled(m_editable && (m_variableCombo->count() > 1));

  for (int row{0}; row < m_valueEdits.size(); ++row) {
    LineEditNum *edit = m_valueEdits[row];
    if (!edit) {
      continue;
    }
    if (!edit->hasFocus()) {
      edit->setText(edit->formatted(valueOf(row, m_open)));
    }
    edit->setEnabled(m_editable);
    if ((row == tempRow) || (row == presRow)) {
      edit->setVisible(row == variable);
    }
  }
  if (m_setpointLabel) {
    m_setpointLabel->setText(pressure ? tr("Setpoint, %1").arg(pressureUnit()) : tr("Setpoint, °C"));
  }

  if (pressure) {
    m_modeCombo->setCurrentIndex(-1);
    m_modeCombo->setToolTip(tr("Pressure goes to its setpoint directly: ramps and holds are for temperature only"));
  } else {
    m_modeCombo->setCurrentIndex(m_modeCombo->findData(mode));
    m_modeCombo->setToolTip(tr("Full speed: to the target as fast as possible, then held for the hold time.\n"
                               "Linear ramp: from the previous target to this one over the ramp time.\n"
                               "Custom ramp rate: a linear ramp at the rate given; its ramp time is calculated."));
  }
  m_modeCombo->setEnabled(m_editable && !pressure);
  if (auto *items = qobject_cast<QStandardItemModel *>(m_modeCombo->model())) {
    items->item(m_modeCombo->findData(CUSTOM_RATE))->setEnabled(rampKnown);
  }

  if (m_durationEdit) {
    // until stopped: a hold or a pressure, offered on the last interval (the ones after it would never run)
    bool const untilStopped{valueOf(rowOf(QStringLiteral("duration")), m_open) >= HOLD_UNTIL_STOPPED};
    bool const holds{pressure || (mode == FULL_SPEED)};
    if (untilStopped) {
      m_durationEdit->clear();
      m_durationEdit->setPlaceholderText(tr("Until stopped"));
    } else if (!m_durationEdit->hasFocus()) {
      m_durationEdit->setTime(static_cast<quint32>(valueOf(rowOf(QStringLiteral("duration")), m_open)));
    }
    m_durationLabel->setText(pressure ? tr("Duration") : (mode == FULL_SPEED) ? tr("Hold time") : tr("Ramp time"));
    m_durationEdit->setEnabled(m_editable && (pressure || !customRate) && !untilStopped);
    m_untilStoppedCheck->setChecked(untilStopped);
    m_untilStoppedCheck->setVisible(untilStopped || (holds && (m_open == m_cards.size() - 1)));
    m_untilStoppedCheck->setEnabled(m_editable);
  }

  bool const                  ramp{!pressure && (mode != FULL_SPEED)};
  std::optional<double> const rate = ramp ? rampRate(m_open) : std::nullopt;
  if (!m_rateEdit->hasFocus()) {
    m_rateEdit->setText(rate ? m_rateEdit->formatted(*rate) : QString{});
  }
  m_rateEdit->setEnabled(m_editable && ramp && customRate);
  m_rateEdit->setToolTip(!ramp        ? QString{}
                         : !rampKnown ? tr("The first interval ramps from the chamber's temperature, which is not known now")
                         : customRate ? ((m_open == 0) ? tr("From the chamber's temperature: the ramp time is calculated again at Start")
                                                       : QString{})
                         : (m_open == 0) ? tr("Calculated: the change from the chamber's temperature now over the ramp time")
                                         : tr("Calculated: the change from the previous target over the ramp time"));

  m_duplicateBtn->setEnabled(m_editable);
  m_holdBtn->setVisible(ramp);
  m_holdBtn->setEnabled(m_editable);
  // the first interval stays: the others can go
  m_deleteBtn->setVisible(m_open > 0);
  m_deleteBtn->setEnabled(m_editable);
}

void IntervalCardList::styleCards() {
  theme::Colors const &c = theme::colors();
  setStyleSheet(QStringLiteral("QFrame#intervalCard { background: %1; border: %3px solid %2; border-radius: %4px; }"
                               "QFrame#intervalCard[open=\"true\"] { border: %5px solid %6; }"
                               "QLabel#intervalNumber { background: %7; color: %8; border-radius: %9px; font-weight: bold; }"
                               "QFrame#intervalCard[open=\"true\"] QLabel#intervalNumber { background: %6; color: white; }")
                  .arg(c.panel.name(), c.border.name())
                  .arg(theme::dp(1))
                  .arg(theme::radius())
                  .arg(theme::dp(3))
                  .arg(c.accent.name(), c.panelAlt.name(), c.muted.name())
                  .arg(theme::radius()));
  for (int interval{0}; interval < m_cards.size(); ++interval) {
    m_cards[interval].frame->setProperty("open", interval == m_open);
    for (QWidget *widget : {static_cast<QWidget *>(m_cards[interval].frame), static_cast<QWidget *>(m_cards[interval].number)}) {
      widget->style()->unpolish(widget);
      widget->style()->polish(widget);
    }
  }
  if (m_duplicateBtn) {
    int const size{theme::dp(26)};
    m_duplicateBtn->setIcon(theme::menuIcon(QStringLiteral("plus"), c.text, c.text, size));
    m_holdBtn->setIcon(theme::menuIcon(QStringLiteral("plus"), c.text, c.text, size));
    m_deleteBtn->setIcon(theme::menuIcon(QStringLiteral("delete"), c.danger, c.danger, size));
    m_deleteBtn->setStyleSheet(QStringLiteral("color: %1;").arg(c.danger.name()));
  }
}

bool IntervalCardList::eventFilter(QObject *watched, QEvent *event) {
  if (auto *head = qobject_cast<QFrame *>(watched); head && head->property("interval").isValid()) {
    if (event->type() == QEvent::MouseButtonPress) {
      m_pressAt = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
    } else if (event->type() == QEvent::MouseButtonRelease) {
      QPoint const moved = static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - m_pressAt;
      if (moved.manhattanLength() < theme::dp(22)) { // a tap, not a scroll
        int const interval{head->property("interval").toInt()};
        if (interval == m_open) { // closed again (still selected)
          closeEditor();
          m_open = -1;
          styleCards();
          refreshTexts();
        } else {
          openCard(interval);
        }
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void IntervalCardList::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange) {
    styleCards();
    refreshTexts();
  }
}

}
