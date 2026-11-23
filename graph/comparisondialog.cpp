#include "comparisondialog.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "comparison.h"
#include "experimentbrowser.h"
#include "resholder.h"
#include "theme.h"
#include "touchsupport.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
QLabel *caption(QString const &text, QWidget *parent) {
  auto *label = new QLabel(text.toUpper(), parent);
  label->setProperty("muted", true);
  QFont font = label->font();
  font.setPointSizeF(font.pointSizeF() * 0.82);
  font.setBold(true);
  font.setLetterSpacing(QFont::PercentageSpacing, 108);
  label->setFont(font);
  return label;
}

// Cobalt on a light tint: ticked rows and the counts (dark theme: the accent, faint)
QColor tint() {
  theme::Colors const &c = theme::colors();
  return theme::isDark() ? QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 45) : theme::guide::cobaltTint;
}

// a card's heading (as on Home): a Cobalt icon, the name, and at the right a tinted badge with what is chosen
QHBoxLayout *heading(QString const &icon, QString const &text, QWidget *parent, QLabel **badge) {
  auto *row = new QHBoxLayout();
  row->setSpacing(theme::dp(10));
  auto *mark = new QLabel(parent);
  theme::setThemedIcon(mark, icon, theme::dp(24));
  row->addWidget(mark);
  row->addWidget(caption(text, parent));
  row->addStretch(1);
  *badge     = new QLabel(parent);
  QFont font = (*badge)->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 0.82);
  (*badge)->setFont(font);
  (*badge)->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-radius: %3px; padding: %4px %5px; }")
                            .arg(tint().name(QColor::HexArgb), theme::colors().accent.name())
                            .arg(theme::dp(12))
                            .arg(theme::dp(3))
                            .arg(theme::dp(12)));
  row->addWidget(*badge);
  return row;
}

// a white card with its outline, as on Home
QFrame *card(QWidget *parent) {
  auto *frame = new QFrame(parent);
  frame->setObjectName("compareCard");
  theme::Colors const &c = theme::colors();
  frame->setStyleSheet(QStringLiteral("QFrame#compareCard { background: %1; border: %2px solid %3; border-radius: %4px; }")
                         .arg(c.panel.name())
                         .arg(theme::dp(1))
                         .arg(c.border.name())
                         .arg(theme::radius()));
  return frame;
}

// tick box and text drawn together (style sheet indicators overlap the text); a tap anywhere on the row toggles it
class CheckDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *painter, QStyleOptionViewItem const &option, QModelIndex const &index) const override {
    theme::Colors const &c = theme::colors();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    bool const checked{index.data(Qt::CheckStateRole).toInt() == Qt::Checked};
    // a ticked row on the Cobalt tint
    if (checked || (option.state & QStyle::State_Selected)) {
      painter->setPen(Qt::NoPen);
      painter->setBrush(checked ? tint() : QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 20));
      painter->drawRoundedRect(QRectF(option.rect).adjusted(0, theme::dp(2), 0, -theme::dp(2)), theme::radius(), theme::radius());
    }
    double const box{static_cast<double>(theme::dp(30))};
    QRectF const indicator(option.rect.left() + theme::dp(7), option.rect.center().y() - box / 2 + 0.5, box, box);
    painter->setPen(QPen(checked ? c.accent : c.border.darker(125), theme::dp(2)));
    painter->setBrush(checked ? c.accent : c.panel);
    painter->drawRoundedRect(indicator, theme::dp(5), theme::dp(5));
    if (checked) {
      QPen tick(Qt::white, std::max(1.5, box / 8.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
      painter->setPen(tick);
      painter->setBrush(Qt::NoBrush);
      QPointF const a(indicator.left() + box * 0.24, indicator.top() + box * 0.52);
      QPointF const b(indicator.left() + box * 0.43, indicator.top() + box * 0.70);
      QPointF const d(indicator.left() + box * 0.77, indicator.top() + box * 0.32);
      painter->drawPolyline(QPolygonF{{a, b, d}});
    }
    QRect const text = option.rect.adjusted(static_cast<int>(indicator.right()) - option.rect.left() + theme::dp(15), 0, -theme::dp(7), 0);
    painter->setPen((index.flags() & Qt::ItemIsEnabled) ? c.text : c.off);
    QFont font = option.font;
    font.setBold(checked);
    painter->setFont(font);
    painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                      option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width()));
    painter->restore();
  }

  QSize sizeHint(QStyleOptionViewItem const &option, QModelIndex const &index) const override {
    int const width{option.fontMetrics.horizontalAdvance(index.data(Qt::DisplayRole).toString()) + theme::dp(60)};
    return {width, std::max(theme::dp(52), option.fontMetrics.height() + theme::dp(18))};
  }

  bool editorEvent(QEvent *event, QAbstractItemModel *model, QStyleOptionViewItem const &option, QModelIndex const &index) override {
    if (!(index.flags() & Qt::ItemIsUserCheckable) || !(index.flags() & Qt::ItemIsEnabled)) {
      return false;
    }
    bool toggle{false};
    if (event->type() == QEvent::MouseButtonRelease) {
      auto *mouse = static_cast<QMouseEvent *>(event);
      toggle      = (mouse->button() == Qt::LeftButton) && option.rect.contains(mouse->position().toPoint());
    } else if (event->type() == QEvent::KeyPress) {
      int const key{static_cast<QKeyEvent *>(event)->key()};
      toggle = (key == Qt::Key_Space) || (key == Qt::Key_Select);
    } else if (event->type() == QEvent::MouseButtonDblClick) {
      return true; // not a second toggle
    }
    if (!toggle) {
      return false;
    }
    bool const checked{index.data(Qt::CheckStateRole).toInt() == Qt::Checked};
    return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
  }
};

QListWidget *checkList(QWidget *parent) {
  auto *list = new QListWidget(parent);
  list->setFrameShape(QFrame::NoFrame);
  // on the card: an inner zone (Home's value boxes), the rows a little in from its edges
  list->setStyleSheet(QStringLiteral("QListWidget { background: %1; border: none; border-radius: %2px; padding: %3px; }")
                        .arg(theme::colors().panelAlt.name())
                        .arg(theme::radius())
                        .arg(theme::dp(6)));
  list->setItemDelegate(new CheckDelegate(list));
  touch::enableScrolling(list);
  return list;
}
}

ComparisonDialog::ComparisonDialog(ResHolder *resHolder, ExperimentLibrary *library, QVector<OpenTab> const &openTabs,
                                   ChartComparison const &initial, QString const &checkedTab, QWidget *parent)
    : QDialog{parent}, m_openTabs{openTabs} {
  bool const editing{!initial.members.isEmpty()};
  setWindowTitle(editing ? tr("Change comparison") : tr("New comparison"));
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(theme::dp(32), theme::dp(24), theme::dp(32), theme::dp(24));
  lay->setSpacing(theme::dp(16));
  auto *title = new QLabel(windowTitle(), this);
  QFont font  = title->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 1.25);
  title->setFont(font);
  lay->addWidget(title);

  auto *columns = new QHBoxLayout();
  columns->setSpacing(theme::dp(24));
  lay->addLayout(columns, 1);

  // ---- experiments: open tabs, then the ones added from the list
  auto *experimentsCard = card(this);
  auto *experimentsLay  = new QVBoxLayout(experimentsCard);
  experimentsLay->setContentsMargins(theme::dp(28), theme::dp(24), theme::dp(28), theme::dp(28));
  experimentsLay->setSpacing(theme::dp(12));
  experimentsLay->addLayout(heading(QStringLiteral("chart"), tr("Experiments"), experimentsCard, &m_experimentsBadge));
  experimentsLay->addWidget(caption(tr("Open tabs"), experimentsCard));
  m_tabsList = checkList(experimentsCard);
  QStringList initialKeys;
  for (ChartComparison::Member const &member : initial.members) {
    initialKeys << member.key;
  }
  for (OpenTab const &tab : m_openTabs) {
    auto *item = new QListWidgetItem(tab.label, m_tabsList);
    item->setToolTip(tab.name);
    item->setData(Qt::UserRole, tab.key);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    bool const checked{editing ? initialKeys.contains(tab.key) : (tab.key == checkedTab)};
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
  }
  experimentsLay->addWidget(m_tabsList, 1);
  experimentsLay->addSpacing(theme::dp(4));
  experimentsLay->addWidget(caption(tr("Added from the experiment list"), experimentsCard));
  m_addedList = checkList(experimentsCard);
  m_addedList->setToolTip(tr("Untick to leave an experiment out"));
  for (ChartComparison::Member const &member : initial.members) {
    bool const isOpenTab{std::any_of(m_openTabs.cbegin(), m_openTabs.cend(), [&](OpenTab const &tab) { return tab.key == member.key; })};
    if (!isOpenTab) {
      addFromList(member.run);
    }
  }
  experimentsLay->addWidget(m_addedList, 1);
  columns->addWidget(experimentsCard, 3);

  // ---- the experiment list: a click adds an experiment
  auto *listCard = card(this);
  auto *listLay  = new QVBoxLayout(listCard);
  listLay->setContentsMargins(theme::dp(28), theme::dp(24), theme::dp(28), theme::dp(28));
  listLay->setSpacing(theme::dp(12));
  QLabel *listBadge{nullptr};
  listLay->addLayout(heading(QStringLiteral("analysis"), tr("Experiment list"), listCard, &listBadge));
  listBadge->setText(tr("Tap to add"));
  auto *browser = new ExperimentBrowser(library, false, listCard);
  connect(browser, &ExperimentBrowser::openRequested, this, [this](ExperimentRun const &run) { addFromList(run); });
  listLay->addWidget(browser, 1);
  columns->addWidget(listCard, 3);

  // ---- channels (the experiments are aligned on their starts)
  auto *channelsCard = card(this);
  auto *channelsLay  = new QVBoxLayout(channelsCard);
  channelsLay->setContentsMargins(theme::dp(28), theme::dp(24), theme::dp(28), theme::dp(28));
  channelsLay->setSpacing(theme::dp(12));
  channelsLay->addLayout(heading(QStringLiteral("channels"), tr("Channels"), channelsCard, &m_channelsBadge));
  m_channelsList = checkList(channelsCard);
  QStringList const  names  = resHolder->states->getNames();
  QVariantList const styles = resHolder->settings->settingsMap()["states"].toList();
  QStringList const  checkedChannels = editing ? initial.channels : QStringList{QStringLiteral("temp")};
  for (int i{0}; i < names.size(); ++i) {
    QString const label = (i < styles.size()) ? styles[i].toMap()["label"].toString() : QString{};
    auto *item          = new QListWidgetItem(label.isEmpty() ? names[i] : label, m_channelsList);
    item->setData(Qt::UserRole, names[i]);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setCheckState(checkedChannels.contains(names[i]) ? Qt::Checked : Qt::Unchecked);
  }
  channelsLay->addWidget(m_channelsList, 1);
  columns->addWidget(channelsCard, 2);

  // ---- buttons
  auto *buttons   = new QHBoxLayout();
  m_hint          = new QLabel(this);
  auto *cancelBtn = new QPushButton(tr("Cancel"), this);
  m_okBtn         = new QPushButton(editing ? tr("Update") : tr("Compare"), this);
  m_okBtn->setObjectName("primaryButton");
  m_okBtn->setDefault(true);
  for (QPushButton *button : {cancelBtn, m_okBtn}) {
    button->setMinimumSize(theme::dp(187), theme::dp(60));
    button->setFocusPolicy(Qt::NoFocus);
  }
  buttons->addWidget(m_hint);
  buttons->addStretch();
  buttons->addWidget(cancelBtn);
  buttons->addWidget(m_okBtn);
  lay->addLayout(buttons);

  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
  for (QListWidget *list : {m_tabsList, m_addedList, m_channelsList}) {
    connect(list, &QListWidget::itemChanged, this, &ComparisonDialog::updateState);
  }
  updateState();
  // most of the window, never bigger than it
  QWidget const *window = parent ? parent->window() : nullptr;
  QSize const    room   = window ? window->size() : QSize(theme::dp(1875), theme::dp(1125));
  resize(std::min(theme::dp(1762), static_cast<int>(room.width() * 0.8)), std::min(theme::dp(1050), static_cast<int>(room.height() * 0.8)));
}

void ComparisonDialog::addFromList(ExperimentRun const &run) {
  // already there (as an open tab or added)
  for (OpenTab const &tab : std::as_const(m_openTabs)) {
    if (tab.key == run.key) {
      for (int i{0}; i < m_tabsList->count(); ++i) {
        if (m_tabsList->item(i)->data(Qt::UserRole).toString() == run.key) {
          m_tabsList->item(i)->setCheckState(Qt::Checked);
        }
      }
      return;
    }
  }
  for (int i{0}; i < m_added.size(); ++i) {
    if (m_added[i].key == run.key) {
      m_addedList->item(i)->setCheckState(Qt::Checked);
      return;
    }
  }
  m_added.push_back(run);
  auto *item = new QListWidgetItem(QStringLiteral("%1  %2").arg(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.start)).toString("yyyy-MM-dd HH:mm"),
                                                               ExperimentLibrary::formatDuration(run.duration())),
                                   m_addedList);
  item->setToolTip(run.title);
  item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
  item->setCheckState(Qt::Checked);
  updateState();
}

void ComparisonDialog::updateState() {
  if (!m_okBtn) {
    return; // still being built
  }
  ChartComparison const chosen = comparison();
  int const             experiments{static_cast<int>(chosen.members.size())};
  int const             channels{static_cast<int>(chosen.channels.size())};
  bool const            ready{(experiments >= 2) && (experiments <= MAX_EXPERIMENTS) && (channels > 0)};
  m_okBtn->setEnabled(ready);
  m_experimentsBadge->setText(tr("%1 of %2").arg(experiments).arg(MAX_EXPERIMENTS));
  m_channelsBadge->setText(tr("%1 chosen").arg(channels));
  // what is missing, or ready (green)
  theme::Colors const &c = theme::colors();
  QString              text;
  if (experiments < 2) {
    text = tr("Tick at least two experiments.");
  } else if (experiments > MAX_EXPERIMENTS) {
    text = tr("At most %1 experiments.").arg(MAX_EXPERIMENTS);
  } else if (channels == 0) {
    text = tr("Tick at least one channel.");
  } else {
    text = tr("Ready: %1 experiments from their starts, %2 channel(s).").arg(experiments).arg(channels);
  }
  m_hint->setText(text);
  m_hint->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: %2; }")
                          .arg((ready ? c.active : c.muted).name(), ready ? QStringLiteral("bold") : QStringLiteral("normal")));
}

ChartComparison ComparisonDialog::comparison() const {
  ChartComparison chosen;
  for (int i{0}; i < m_tabsList->count(); ++i) {
    if (m_tabsList->item(i)->checkState() == Qt::Checked) {
      OpenTab const &tab = m_openTabs[i];
      chosen.members.push_back({tab.key, tab.label, tab.run});
    }
  }
  for (int i{0}; (i < m_addedList->count()) && (i < m_added.size()); ++i) {
    if (m_addedList->item(i)->checkState() == Qt::Checked) {
      ExperimentRun const &run = m_added[i];
      chosen.members.push_back({run.key, QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.start)).toString("yyyy-MM-dd HH:mm"), run});
    }
  }
  for (int i{0}; i < m_channelsList->count(); ++i) {
    if (m_channelsList->item(i)->checkState() == Qt::Checked) {
      chosen.channels << m_channelsList->item(i)->data(Qt::UserRole).toString();
    }
  }
  chosen.alignMode = static_cast<int>(core::AlignMode::START); // always from the experiments' starts
  chosen.alignN    = 1;
  return chosen;
}

}
