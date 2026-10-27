#include "graphstyleeditor.h"

#include <QLayoutItem>
#include <QVBoxLayout>

#include "constants.h"
#include <QLabel>
#include <QScrollArea>
#include "utils.h"

namespace deepvaccontrol {
GraphStyleEditor::GraphStyleEditor(ResHolder *resHolder, QWidget *parent) : QDialog{parent}, m_resHolder{resHolder} {
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(16 * ssf, 14 * ssf, 16 * ssf, 14 * ssf);
  mainLay->setSpacing(10 * ssf);

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
  elementLay->setContentsMargins(0, 0, 6 * ssf, 0);
  elementLay->setSpacing(6 * ssf);

  QVariantList const statesList = resHolder->settings->settingsMap()["states"].toList();

  for (int i{0U}; i < statesList.size(); ++i) {
    QVariantMap const stateMap{statesList[i].toMap()};
    if (stateMap.contains("visible")) {
      auto *stateStyleWidget = new GraphStyleSingleWidget(i, stateMap, list);
      connect(stateStyleWidget, &GraphStyleSingleWidget::styleUpdated, this, [this, i, stateStyleWidget]() {
        emit styleChanged(i, stateStyleWidget->getStateMapVariant());
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
  for (QPushButton *item : {closeBtn, selectAllBtn, selectNoneBtn}) {
    item->setMinimumSize(100 * ssf, 30 * ssf);
  }

  auto *btnLay = new QHBoxLayout();
  btnLay->setSpacing(8 * ssf);
  btnLay->addWidget(selectAllBtn);
  btnLay->addWidget(selectNoneBtn);
  btnLay->addStretch();
  btnLay->addWidget(closeBtn);

  mainLay->addLayout(btnLay);
  connect(closeBtn, &QPushButton::clicked, this, &GraphStyleEditor::hide);

  connect(selectAllBtn, &QPushButton::clicked, this, [this] {
    for (auto &item : widgetsMap) {
      item->setEnabled(true);
    }
    emit styleAllChanged(getStateStylesList());
    updateCount();
  });

  connect(selectNoneBtn, &QPushButton::clicked, this, [this] {
    for (auto &item : widgetsMap) {
      item->setEnabled(false);
    }
    emit styleAllChanged(getStateStylesList());
    updateCount();
  });

  updateCount();
  setMinimumSize(640 * ssf, 520 * ssf);
  setModal(true);
  setWindowTitle(tr("Channels"));
}

void GraphStyleEditor::updateCount() {
  int shown{0};
  for (auto const &item : std::as_const(widgetsMap)) {
    shown += item->getStateMapVariant().value("visible").toBool() ? 1 : 0;
  }
  m_countLabel->setText(tr("%1 of %2 shown").arg(shown).arg(widgetsMap.size()));
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
  m_resHolder->settings->setStatesSettings(getStateStylesList());

  // Call the base class implementation
  QDialog::hideEvent(event);
}
void GraphStyleEditor::setCompact() {
  double const ssf{utils::ssf()};
  setWindowFlags(Qt::Widget);
  setModal(false);
  setMinimumSize(0, 0);
  layout()->setContentsMargins(0, 0, static_cast<int>(8 * ssf), 0);
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
  for (GraphStyleSingleWidget *row : std::as_const(widgetsMap)) {
    row->setCompact();
  }
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
  emit styleAllChanged(getStateStylesList());
  updateCount();
}

}
