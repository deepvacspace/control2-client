#include "intervalcardlist.h"

#include <QComboBox>
#include <algorithm>
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
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

QString number(double value, int prec) {
  return QString::number(value, 'f', prec).replace(QLatin1Char('-'), QChar(0x2212));
}
}

IntervalCardList::IntervalCardList(QWidget *parent) : QWidget{parent} {
  m_lay = new QVBoxLayout(this);
  m_lay->setContentsMargins(0, 0, px(8), 0);
  m_lay->setSpacing(px(6));
  m_lay->addStretch();
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
        refreshTexts();
        fillEditor();
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

quint32 IntervalCardList::flagsOf(int interval) const {
  int const            row{m_model ? m_model->getFlagsIndex() : -1};
  QStandardItem const *item = (row >= 0) ? m_model->getStandardModel()->item(row, interval) : nullptr;
  return item ? item->data(Qt::UserRole + 1).toUInt() : 0U;
}

void IntervalCardList::setFlags(int interval, quint32 flags) {
  int const row{m_model->getFlagsIndex()};
  if (QStandardItem *item = (row >= 0) ? m_model->getStandardModel()->item(row, interval) : nullptr) {
    item->setData(flags, Qt::UserRole + 1);
  }
}

void IntervalCardList::commit() {
  m_committing = true;
  m_model->emitModelUpdated();
  m_committing = false;
  refreshTexts();
}

void IntervalCardList::closeEditor() {
  if (m_editor) {
    m_editor->hide();
    m_editor->deleteLater();
  }
  m_editor = nullptr;
  m_valueEdits.clear();
  m_durationEdit = nullptr;
  m_modeCombo    = nullptr;
  m_labelEdit    = nullptr;
  m_duplicateBtn = nullptr;
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
  int const count{m_model->getStandardModel()->columnCount()};
  for (int interval{0}; interval < count; ++interval) {
    Card card;
    card.frame = new QFrame(this);
    card.frame->setObjectName("intervalCard");
    auto *cardLay = new QVBoxLayout(card.frame);
    cardLay->setContentsMargins(px(8), px(6), px(8), px(6));
    cardLay->setSpacing(px(6));
    // the head: a tap opens or closes the card
    card.head = new QFrame(card.frame);
    card.head->setProperty("interval", interval);
    card.head->installEventFilter(this);
    card.head->setCursor(Qt::PointingHandCursor);
    auto *headLay = new QHBoxLayout(card.head);
    headLay->setContentsMargins(0, 0, 0, 0);
    headLay->setSpacing(px(8));
    card.number = new QLabel(QStringLiteral("%1").arg(interval + 1, 2, 10, QLatin1Char('0')), card.head);
    card.number->setObjectName("intervalNumber");
    card.number->setAlignment(Qt::AlignCenter);
    card.number->setFixedSize(px(28), px(22));
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
  int const          chevron{px(14)};
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
      QString const target = number(value(tempRow), params.value(tempRow).toMap().value("prec", 1).toInt());
      title = utils::getBitOfDword(flags, RAMP_BIT)    ? tr("Ramp to %1 °C").arg(target)
              : utils::getBitOfDword(flags, AWAIT_BIT) ? tr("Go to %1 °C and hold").arg(target)
                                                       : tr("Go to %1 °C").arg(target);
    } else {
      QStringList others;
      for (int row{0}; row < order.size(); ++row) {
        if ((row != durationRow) && (row != flagsRow) && (row != tempRow) && utils::getBitOfDword(flags, row)) {
          QVariantMap const param = params.value(row).toMap();
          others << QStringLiteral("%1 %2").arg(param.value("label").toString(), number(value(row), param.value("prec", 1).toInt()));
        }
      }
      title = others.isEmpty() ? tr("No setpoint") : others.join(QStringLiteral(" · "));
    }
    card.title->setText(title);
    // how long, and its label
    long const  duration{(durationRow >= 0) ? static_cast<long>(value(durationRow)) : 0L};
    QStringList parts{utils::timeFromSec(duration)};
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
          int const      margin{px(8)};
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
  lay->setContentsMargins(0, px(2), 0, px(2));
  lay->setSpacing(px(8));
  auto *grid = new QGridLayout();
  grid->setHorizontalSpacing(px(8));
  grid->setVerticalSpacing(px(2));
  int  field{0};
  auto addField = [&](QString const &title, QWidget *edit) {
    auto *label = new QLabel(title, m_editor);
    label->setProperty("muted", true);
    int const row{(field / 2) * 2};
    int const column{field % 2};
    grid->addWidget(label, row, column);
    grid->addWidget(edit, row + 1, column);
    edit->setMinimumHeight(px(32));
    edit->setMinimumWidth(0);
    edit->setMaximumWidth(QWIDGETSIZE_MAX);
    edit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    edit->setFont(font()); // the page's font, not the large one of the screen inputs
    for (QWidget *part : edit->findChildren<QWidget *>()) {
      part->setFont(font());
    }
    ++field;
  };

  QStringList const  order  = m_model->getParamListOrder();
  QVariantList const params = m_model->getParamListVariant();
  m_valueEdits.fill(nullptr, order.size());
  // the setpoints (a number each), then the duration
  for (int row{0}; row < order.size(); ++row) {
    QVariantMap const param = params.value(row).toMap();
    if (param.value("type").toInt() != 0) {
      continue;
    }
    auto *edit = new LineEditNum(param.value("min").toDouble(), param.value("max").toDouble(), param.value("prec", 1).toInt(), true, m_editor);
    edit->setIsScientific(param.value("notation").toString() == QLatin1String("scientific"));
    m_valueEdits[row] = edit;
    addField(order.value(row) == QLatin1String("temp") ? tr("Target °C") : param.value("label").toString(), edit);
    connect(edit, &LineEditNum::userEditingFinished, this, [this, row, edit, type = param.value("type").toInt()] {
      if ((m_open < 0) || !m_model) {
        return;
      }
      bool         ok{false};
      double const value = QString(edit->text()).replace(QChar(0x2212), QLatin1Char('-')).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
      if (!ok) {
        fillEditor();
        return;
      }
      m_committing = true;
      m_model->getStandardModel()->item(row, m_open)->setData(value, type);
      quint32 flags{flagsOf(m_open)};
      utils::setBitOfDword(flags, row, true); // given a value: used
      if (row == m_model->getParamListOrder().indexOf(QStringLiteral("temp"))
          && !utils::getBitOfDword(flags, RAMP_BIT) && !utils::getBitOfDword(flags, AWAIT_BIT)) {
        utils::setBitOfDword(flags, RAMP_BIT, true);
      }
      setFlags(m_open, flags);
      m_committing = false;
      commit();
    });
  }
  if (int const durationRow{static_cast<int>(order.indexOf(QStringLiteral("duration")))}; durationRow >= 0) {
    m_durationEdit = new LineEditTime(true, m_editor);
    addField(tr("Duration"), m_durationEdit);
    connect(m_durationEdit, &LineEditTime::userEdited, this, [this, durationRow, type = params.value(durationRow).toMap().value("type").toInt()] {
      if ((m_open < 0) || !m_model) {
        return;
      }
      m_committing = true;
      m_model->getStandardModel()->item(durationRow, m_open)->setData(static_cast<int>(m_durationEdit->getSeconds()), type);
      m_committing = false;
      commit();
    });
  }
  // how the temperature changes: linearly over the interval, or at full speed and then held
  m_modeCombo = new QComboBox(m_editor);
  m_modeCombo->addItem(tr("Ramp over interval"));
  m_modeCombo->addItem(tr("Full speed, then hold"));
  m_modeCombo->setToolTip(tr("Ramp: linearly over the interval. Full speed: to the target as fast as possible, then hold it"));
  addField(tr("Mode"), m_modeCombo);
  connect(m_modeCombo, &QComboBox::activated, this, [this](int index) {
    if ((m_open < 0) || !m_model) {
      return;
    }
    quint32 flags{flagsOf(m_open)};
    utils::setBitOfDword(flags, RAMP_BIT, index == 0);
    utils::setBitOfDword(flags, AWAIT_BIT, index == 1);
    m_committing = true;
    setFlags(m_open, flags);
    m_committing = false;
    commit();
  });
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
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 1);
  lay->addLayout(grid);

  // Duplicate · Delete
  auto *actions  = new QHBoxLayout();
  m_duplicateBtn = new QPushButton(tr("Duplicate"), m_editor);
  m_duplicateBtn->setToolTip(tr("A copy of this interval right after it"));
  m_deleteBtn = new QPushButton(tr("Delete"), m_editor);
  m_deleteBtn->setProperty("outline", true);
  for (QPushButton *button : {m_duplicateBtn, m_deleteBtn}) {
    button->setFixedHeight(px(28));
    button->setFocusPolicy(Qt::NoFocus);
  }
  actions->addWidget(m_duplicateBtn);
  actions->addStretch();
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

void IntervalCardList::fillEditor() {
  if (!m_editor || !m_model || (m_open < 0) || (m_open >= m_model->getStandardModel()->columnCount())) {
    return;
  }
  QStringList const  order  = m_model->getParamListOrder();
  QVariantList const params = m_model->getParamListVariant();
  quint32 const      flags{flagsOf(m_open)};
  for (int row{0}; row < m_valueEdits.size(); ++row) {
    if (LineEditNum *edit = m_valueEdits[row]; edit && !edit->hasFocus()) {
      QVariantMap const param = params.value(row).toMap();
      double const      value = m_model->getStandardModel()->item(row, m_open)->data(param.value("type").toInt()).toDouble();
      edit->setText(QString::number(value, edit->getIsScientific() ? 'E' : 'f', edit->getPrec()));
      edit->setEnabled(m_editable);
    }
  }
  if (m_durationEdit) {
    int const row{static_cast<int>(order.indexOf(QStringLiteral("duration")))};
    if (!m_durationEdit->hasFocus()) {
      m_durationEdit->setTime(static_cast<quint32>(
        m_model->getStandardModel()->item(row, m_open)->data(params.value(row).toMap().value("type").toInt()).toDouble()));
    }
    m_durationEdit->setEnabled(m_editable);
  }
  m_modeCombo->setCurrentIndex(utils::getBitOfDword(flags, AWAIT_BIT) ? 1 : 0);
  m_modeCombo->setEnabled(m_editable);
  if (!m_labelEdit->hasFocus() && (m_labelEdit->text() != m_labels.value(m_open))) {
    m_labelEdit->setText(m_labels.value(m_open));
  }
  m_labelEdit->setEnabled(m_editable);
  m_duplicateBtn->setEnabled(m_editable);
  m_deleteBtn->setEnabled(m_editable && (m_model->getStandardModel()->columnCount() > 1));
}

void IntervalCardList::styleCards() {
  theme::Colors const &c = theme::colors();
  setStyleSheet(QStringLiteral("QFrame#intervalCard { background: %1; border: %3px solid %2; border-radius: %4px; }"
                               "QFrame#intervalCard[open=\"true\"] { border: %5px solid %6; }"
                               "QLabel#intervalNumber { background: %7; color: %8; border-radius: %9px; font-weight: bold; }"
                               "QFrame#intervalCard[open=\"true\"] QLabel#intervalNumber { background: %6; color: white; }")
                  .arg(c.panel.name(), c.border.name())
                  .arg(px(1))
                  .arg(px(6))
                  .arg(px(2))
                  .arg(c.accent.name(), c.panelAlt.name(), c.muted.name())
                  .arg(px(4)));
  for (int interval{0}; interval < m_cards.size(); ++interval) {
    m_cards[interval].frame->setProperty("open", interval == m_open);
    for (QWidget *widget : {static_cast<QWidget *>(m_cards[interval].frame), static_cast<QWidget *>(m_cards[interval].number)}) {
      widget->style()->unpolish(widget);
      widget->style()->polish(widget);
    }
  }
  if (m_duplicateBtn) {
    int const size{px(14)};
    m_duplicateBtn->setIcon(theme::menuIcon(QStringLiteral("plus"), c.text, c.text, size));
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
      if (moved.manhattanLength() < px(12)) { // a tap, not a scroll
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
