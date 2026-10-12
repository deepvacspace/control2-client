#include "comparisondialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "comparison.h"
#include "experimentbrowser.h"
#include "resholder.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
int px(double value) {
  return qMax(1, static_cast<int>(value * utils::ssf()));
}

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

QFrame *card(QWidget *parent) {
  auto *frame = new QFrame(parent);
  frame->setObjectName("compareCard");
  theme::Colors const &c = theme::colors();
  frame->setStyleSheet(QStringLiteral("QFrame#compareCard { background: %1; border: %2px solid %3; border-radius: %4px; }")
                         .arg(c.panelAlt.name())
                         .arg(px(1))
                         .arg(c.border.name())
                         .arg(px(8)));
  return frame;
}

QListWidget *checkList(QWidget *parent) {
  auto *list = new QListWidget(parent);
  list->setFrameShape(QFrame::NoFrame);
  list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; }"));
  return list;
}
} // namespace

ComparisonDialog::ComparisonDialog(ResHolder *resHolder, ExperimentLibrary *library, QVector<OpenTab> const &openTabs,
                                   ChartComparison const &initial, QString const &checkedTab, QWidget *parent)
    : QDialog{parent}, m_openTabs{openTabs} {
  bool const editing{!initial.members.isEmpty()};
  setWindowTitle(editing ? tr("Change comparison") : tr("New comparison"));
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(px(20), px(18), px(20), px(16));
  lay->setSpacing(px(12));
  auto *title = new QLabel(windowTitle(), this);
  QFont font  = title->font();
  font.setBold(true);
  font.setPointSizeF(font.pointSizeF() * 1.25);
  title->setFont(font);
  lay->addWidget(title);

  auto *columns = new QHBoxLayout();
  columns->setSpacing(px(12));
  lay->addLayout(columns, 1);

  // ---- experiments: open tabs, then the ones added from the list
  auto *experimentsCard = card(this);
  auto *experimentsLay  = new QVBoxLayout(experimentsCard);
  experimentsLay->setContentsMargins(px(14), px(12), px(14), px(14));
  experimentsLay->setSpacing(px(6));
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
  listLay->setContentsMargins(px(14), px(12), px(14), px(14));
  listLay->setSpacing(px(6));
  listLay->addWidget(caption(tr("Experiment list"), listCard));
  auto *browser = new ExperimentBrowser(library, false, listCard);
  connect(browser, &ExperimentBrowser::openRequested, this, [this](ExperimentRun const &run) { addFromList(run); });
  listLay->addWidget(browser, 1);
  columns->addWidget(listCard, 3);

  // ---- channels and alignment
  auto *channelsCard = card(this);
  auto *channelsLay  = new QVBoxLayout(channelsCard);
  channelsLay->setContentsMargins(px(14), px(12), px(14), px(14));
  channelsLay->setSpacing(px(6));
  channelsLay->addWidget(caption(tr("Channels"), channelsCard));
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
  channelsLay->addSpacing(px(6));
  channelsLay->addWidget(caption(tr("Align on"), channelsCard));
  auto *alignRow = new QHBoxLayout();
  m_alignCombo   = new QComboBox(channelsCard);
  m_alignCombo->addItem(tr("Experiment start"), static_cast<int>(core::AlignMode::START));
  m_alignCombo->addItem(tr("Setpoint change #"), static_cast<int>(core::AlignMode::SETPOINT_CHANGE));
  m_alignCombo->addItem(tr("Target crossing after change #"), static_cast<int>(core::AlignMode::TARGET_CROSSING));
  m_alignCombo->addItem(tr("Stage #"), static_cast<int>(core::AlignMode::STAGE));
  m_alignCombo->setCurrentIndex(std::max(0, m_alignCombo->findData(initial.alignMode)));
  m_alignSpin = new QSpinBox(channelsCard);
  m_alignSpin->setRange(1, 99);
  m_alignSpin->setValue(initial.alignN);
  for (QWidget *widget : std::initializer_list<QWidget *>{m_alignCombo, m_alignSpin}) {
    widget->setMinimumHeight(px(28));
  }
  alignRow->addWidget(m_alignCombo, 1);
  alignRow->addWidget(m_alignSpin);
  channelsLay->addLayout(alignRow);
  columns->addWidget(channelsCard, 2);

  // ---- buttons
  auto *buttons   = new QHBoxLayout();
  auto *hint      = new QLabel(tr("Two to %1 experiments, at least one channel.").arg(MAX_EXPERIMENTS), this);
  hint->setProperty("muted", true);
  auto *cancelBtn = new QPushButton(tr("Cancel"), this);
  m_okBtn         = new QPushButton(editing ? tr("Update") : tr("Compare"), this);
  cancelBtn->setProperty("outline", true);
  m_okBtn->setObjectName("primaryButton");
  m_okBtn->setDefault(true);
  for (QPushButton *button : {cancelBtn, m_okBtn}) {
    button->setMinimumSize(px(100), px(32));
    button->setFocusPolicy(Qt::NoFocus);
  }
  buttons->addWidget(hint);
  buttons->addStretch();
  buttons->addWidget(cancelBtn);
  buttons->addWidget(m_okBtn);
  lay->addLayout(buttons);

  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
  for (QListWidget *list : {m_tabsList, m_addedList, m_channelsList}) {
    connect(list, &QListWidget::itemChanged, this, &ComparisonDialog::updateState);
  }
  connect(m_alignCombo, &QComboBox::currentIndexChanged, this, &ComparisonDialog::updateState);
  updateState();
  resize(px(1000), px(560));
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
  m_okBtn->setEnabled((chosen.members.size() >= 2) && (chosen.members.size() <= MAX_EXPERIMENTS) && !chosen.channels.isEmpty());
  m_alignSpin->setEnabled(m_alignCombo->currentData().toInt() != static_cast<int>(core::AlignMode::START));
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
  chosen.alignMode = m_alignCombo->currentData().toInt();
  chosen.alignN    = m_alignSpin->value();
  return chosen;
}

} // namespace deepvaccontrol
