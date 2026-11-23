#include "graphstyleeditor.h"

#include <QLayoutItem>
#include <QVBoxLayout>

#include "constants.h"
#include <QLabel>
#include <QScrollArea>
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
namespace {
// the channels' styles with the choice kept under key (by name) laid over them
QVariantList stylesOf(ResHolder *resHolder, QString const &key, QStringList const &defaultShown) {
  QVariantMap const  settings = resHolder->settings->settingsMap();
  QVariantList       styles   = settings.value(QStringLiteral("states")).toList();
  if (key == QLatin1String("states")) {
    return styles;
  }
  QMap<QString, QVariantMap> kept;
  for (QVariant const &item : settings.value(key).toList()) {
    QVariantMap const map = item.toMap();
    kept.insert(map.value(QStringLiteral("name")).toString(), map);
  }
  for (QVariant &item : styles) {
    QVariantMap   map  = item.toMap();
    QString const name = map.value(QStringLiteral("name")).toString();
    if (!map.contains(QStringLiteral("visible"))) {
      continue;
    }
    if (kept.contains(name)) {
      for (char const *field : {"visible", "color", "pen", "width"}) {
        if (kept[name].contains(field)) {
          map[field] = kept[name].value(field);
        }
      }
    } else {
      map[QStringLiteral("visible")] = kept.isEmpty() && defaultShown.contains(name);
    }
    item = map;
  }
  return styles;
}
}

GraphStyleEditor::GraphStyleEditor(ResHolder *resHolder, QWidget *parent)
    : GraphStyleEditor{resHolder, QStringLiteral("states"), {}, parent} {}

GraphStyleEditor::GraphStyleEditor(ResHolder *resHolder, QString const &settingsKey, QStringList const &defaultShown, QWidget *parent)
    : QDialog{parent}, m_resHolder{resHolder}, m_settingsKey{settingsKey}, m_styles(stylesOf(resHolder, settingsKey, defaultShown)) { // parentheses: braces make a list holding the list
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(theme::dp(32), theme::dp(28), theme::dp(32), theme::dp(28));
  mainLay->setSpacing(theme::dp(20));

  // header: title and how many channels are shown
  auto *header = new QHBoxLayout();
  auto *title  = new QLabel(tr("Displayed channels"), this);
  title->setObjectName("cardTitle");
  header->addWidget(title);
  header->addStretch();
  m_countLabel = new QLabel(this);
  m_countLabel->setProperty("muted", true);
  header->addWidget(m_countLabel);
  mainLay->addLayout(header);

  // one row per channel
  auto *list       = new QWidget();
  auto *elementLay = new QVBoxLayout(list);
  elementLay->setContentsMargins(0, 0, theme::dp(12), 0);
  elementLay->setSpacing(theme::dp(12));

  QVariantList const statesList = m_styles;

  for (int i{0U}; i < statesList.size(); ++i) {
    QVariantMap const stateMap{statesList[i].toMap()};
    if (stateMap.contains("visible")) {
      auto *stateStyleWidget = new GraphStyleSingleWidget(i, stateMap, list);
      connect(stateStyleWidget, &GraphStyleSingleWidget::styleUpdated, this, [this, i, stateStyleWidget]() {
        if (!m_compact) {
          emit styleChanged(i, stateStyleWidget->getStateMapVariant());
        }
        updateCount();
      });
      elementLay->addWidget(stateStyleWidget);
      widgetsMap[i] = stateStyleWidget;
    }
  }
  elementLay->addStretch();

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(list);
  list->setAutoFillBackground(false);
  scroll->viewport()->setAutoFillBackground(false);
  scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  mainLay->addWidget(scroll, 1);

  // footer: all / none, done
  auto *closeBtn      = new QPushButton(tr("Done"), this);
  closeBtn->setObjectName("primaryButton");
  auto *selectAllBtn  = new QPushButton(tr("Show all"), this);
  auto *selectNoneBtn = new QPushButton(tr("Hide all"), this);
  m_showAllBtn        = selectAllBtn;
  m_hideAllBtn        = selectNoneBtn;
  for (QPushButton *item : {closeBtn, selectAllBtn, selectNoneBtn}) {
    item->setMinimumSize(theme::dp(187), theme::dp(56));
  }

  auto *btnLay = new QHBoxLayout();
  m_footerLay  = btnLay;
  btnLay->setSpacing(theme::dp(16));
  btnLay->addWidget(selectAllBtn);
  btnLay->addWidget(selectNoneBtn);
  btnLay->addStretch();
  btnLay->addWidget(closeBtn);

  mainLay->addLayout(btnLay);
  connect(closeBtn, &QPushButton::clicked, this, &GraphStyleEditor::hide);

  auto showAll = [this](bool shown) {
    for (auto &item : widgetsMap) {
      item->setEnabled(shown);
    }
    if (!m_compact) { // compact: on Save
      emit styleAllChanged(getStateStylesList());
    }
    updateCount();
  };
  connect(selectAllBtn, &QPushButton::clicked, this, [showAll] { showAll(true); });
  connect(selectNoneBtn, &QPushButton::clicked, this, [showAll] { showAll(false); });

  updateCount();
  setMinimumSize(theme::dp(1200), theme::dp(975));
  setModal(true);
  setWindowTitle(tr("Channels"));
}

void GraphStyleEditor::updateCount() {
  int shown{0};
  for (auto const &item : std::as_const(widgetsMap)) {
    shown += item->getStateMapVariant().value("visible").toBool() ? 1 : 0;
  }
  m_countLabel->setText(tr("%1 of %2 shown").arg(shown).arg(widgetsMap.size()));
  emit countChanged(m_countLabel->text());
  if (m_saveBtn) {
    m_saveBtn->setEnabled(getStateStylesList() != m_applied); // something to save
  }
}

void GraphStyleEditor::persist() {
  if (m_settingsKey == QLatin1String("states")) {
    m_resHolder->settings->setStatesSettings(getStateStylesList());
  } else {
    m_resHolder->settings->setEntries({{m_settingsKey, getStateStylesList()}});
  }
}

void GraphStyleEditor::save() {
  m_applied = getStateStylesList();
  emit styleAllChanged(m_applied);
  persist();
  updateCount();
}

QVariantList GraphStyleEditor::getStateStylesList() {
  QVariantList statesListVariantNew;
  QVariantList statesListVariantOld = m_resHolder->settings->settingsMap()["states"].toList();

  for (int i{0U}; i < statesListVariantOld.size(); ++i) {
    QVariantMap stateMapResult = statesListVariantOld[i].toMap();

    // update state map from widget if state has fields to display on graph
    if (widgetsMap.contains(i)) {
      stateMapResult = widgetsMap[i]->getStateMapVariant();
    }
    statesListVariantNew.push_back(stateMapResult);
  }

  return statesListVariantNew;
}

void GraphStyleEditor::hideEvent(QHideEvent *event) {
  if (m_compact) {
    // closed without Save: the rows back to what the charts show
    for (auto it = widgetsMap.begin(); it != widgetsMap.end(); ++it) {
      if (it.key() < m_applied.size()) {
        it.value()->setStateMap(m_applied[it.key()].toMap());
      }
    }
    updateCount();
  } else {
    persist();
  }

  // Call the base class implementation
  QDialog::hideEvent(event);
}
void GraphStyleEditor::setCompact() {
  setWindowFlags(Qt::Widget);
  setModal(false);
  setMinimumSize(0, 0);
  auto *mainLay = static_cast<QVBoxLayout *>(layout());
  mainLay->setContentsMargins(0, 0, theme::dp(16), 0);
  mainLay->setSpacing(theme::dp(16));
  // the panel has its own title, and its × instead of Done
  for (QLabel *label : findChildren<QLabel *>()) {
    if (label->objectName() == QLatin1String("cardTitle")) {
      label->hide();
    }
  }
  for (QPushButton *button : findChildren<QPushButton *>()) {
    if (button->objectName() == QLatin1String("primaryButton")) {
      button->hide();
    }
  }
  // on top: "6 of 8 shown", then Show all and Hide all, small
  auto *top = new QHBoxLayout();
  top->setContentsMargins(0, 0, 0, 0);
  top->setSpacing(theme::dp(8));
  if (QLayout *header = mainLay->itemAt(0)->layout()) { // the dialog's header: its title, the count
    header->removeWidget(m_countLabel);
  }
  top->addWidget(m_countLabel);
  top->addStretch();
  for (QPushButton *button : {m_showAllBtn, m_hideAllBtn}) {
    m_footerLay->removeWidget(button);
    button->setMinimumSize(0, 0);
    button->setFixedHeight(theme::dp(44));
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(QStringLiteral("padding: 0 %1px;").arg(theme::dp(14)));
    top->addWidget(button);
  }
  mainLay->insertLayout(0, top);
  // the rows as wide as the panel (they wrap their names): no sideways scrolling
  for (QScrollArea *area : findChildren<QScrollArea *>()) {
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
  if (auto *list = findChild<QScrollArea *>(); list && list->widget() && list->widget()->layout()) {
    list->widget()->layout()->setSpacing(theme::dp(16)); // the cards apart
  }
  // at the bottom: Save, the whole width; the charts change only then
  while (QLayoutItem *item = m_footerLay->takeAt(0)) {
    delete item; // the spacer (the buttons moved, Done hidden)
  }
  m_saveBtn = new QPushButton(tr("Save"), this);
  m_saveBtn->setObjectName("primaryButton");
  m_saveBtn->setProperty("panelFooter", true); // roomy, rounded (theme)
  m_saveBtn->setMinimumHeight(theme::dp(72));
  m_saveBtn->setFocusPolicy(Qt::NoFocus);
  m_saveBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_footerLay->addWidget(m_saveBtn);
  connect(m_saveBtn, &QPushButton::clicked, this, &GraphStyleEditor::save);
  for (GraphStyleSingleWidget *row : std::as_const(widgetsMap)) {
    row->setCompact();
  }
  m_compact = true;
  m_applied = getStateStylesList();
  updateCount();
}

void GraphStyleEditor::showOnly(QStringList const &channels) {
  QStringList wanted;
  for (QString const &channel : channels) {
    wanted << channel.trimmed().toLower();
  }
  QStringList const  names  = m_resHolder->states->getNames();
  QVariantList const styles = m_resHolder->settings->settingsMap()["states"].toList();
  for (auto it = widgetsMap.begin(); it != widgetsMap.end(); ++it) {
    QString const name  = names.value(it.key()).toLower();
    QString const label = styles.value(it.key()).toMap().value("label").toString().section(QLatin1Char(','), 0, 0).trimmed().toLower();
    it.value()->setEnabled(wanted.contains(name) || (!label.isEmpty() && wanted.contains(label)));
  }
  if (m_compact) { // asked for (the assistant): applied and kept at once
    save();
    return;
  }
  emit styleAllChanged(getStateStylesList());
  updateCount();
}

}
