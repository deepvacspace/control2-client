#include "settingslistwidget.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "lineeditnum.h"
#include "lineedittime.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
SettingsListWidget::SettingsListWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  auto        *pageLay = new QVBoxLayout(this);
  pageLay->setContentsMargins(0, theme::dp(16), 0, 0);
  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  pageLay->addWidget(card);
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(theme::dp(28), theme::dp(20), theme::dp(28), theme::dp(24));
  cardLay->setSpacing(theme::dp(16));
  auto *title = new QLabel(tr("Operating parameters"), card);
  title->setObjectName("cardTitle");
  cardLay->addWidget(title);

  // one row under the other (as Settings ▸ Application), scrolled when they do not fit
  auto *list    = new QWidget();
  auto *listLay = new QVBoxLayout(list);
  listLay->setContentsMargins(0, 0, theme::dp(8), 0);
  listLay->setSpacing(theme::dp(12));

  SystemValueList &settingsValueList{resHolder->settings->plcSettingsSystemValueList()};
  QStringList      settingsOrder{settingsValueList.getNames()};

  QStringList const ignoreList{"alarm_cfg_1", "alarm_cfg_2"};
  QFont const       fieldFont = font();
  // in same order as in settings.json
  for (int i{0U}; i < settingsValueList.size(); ++i) {
    auto const settingName{settingsOrder[i]};
    auto       settingSysValue = settingsValueList.getSysValue(settingName);

    if (ignoreList.contains(settingName)) {
      continue;
    }

    // "Label, unit" -> label and unit
    QString   label = settingSysValue->getLabel();
    QString   unit;
    int const comma = static_cast<int>(label.lastIndexOf(','));
    if (comma > 0) {
      unit  = label.mid(comma + 1).trimmed();
      label = label.left(comma).trimmed();
    }

    auto *row = new QFrame(list);
    row->setObjectName("outputRow");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(theme::dp(24), theme::dp(12), theme::dp(20), theme::dp(12));
    rowLay->setSpacing(theme::dp(20));
    auto *texts = new QVBoxLayout();
    texts->setSpacing(theme::dp(4));
    auto *labelWidget = new QLabel(label, row);
    labelWidget->setObjectName("rowTitle");
    labelWidget->setWordWrap(true);
    auto *nameWidget = new QLabel(settingName, row);
    nameWidget->setProperty("muted", true);
    nameWidget->setToolTip(tr("PLC variable"));
    texts->addWidget(labelWidget);
    texts->addWidget(nameWidget);
    rowLay->addLayout(texts, 1);

    // Different delegates
    QWidget *editField = nullptr;
    if (settingName.startsWith("t_")) {
      auto *timeField = new LineEditTime(settingSysValue, true, row);
      // same look as the number fields (the inner hour / minute / second boxes as well)
      timeField->setFont(fieldFont);
      timeField->setFixedSize(theme::dp(243), theme::dp(56));
      for (LineEditNum *part : timeField->findChildren<LineEditNum *>()) {
        part->setFont(fieldFont);
        part->setFixedHeight(theme::dp(56));
      }
      editField = timeField;
    } else {
      auto *numField = new LineEditNum(settingSysValue, row);
      numField->setFont(fieldFont);
      numField->setFixedSize(theme::dp(243), theme::dp(56));
      editField = numField;
    }
    rowLay->addWidget(editField);
    auto *unitWidget = new QLabel(unit, row);
    unitWidget->setProperty("muted", true);
    unitWidget->setFixedWidth(theme::dp(75));
    rowLay->addWidget(unitWidget);

    listLay->addWidget(row);
  }
  listLay->addStretch(1);

  auto *scroll = new QScrollArea(card);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(list);
  scroll->viewport()->setAutoFillBackground(false);
  list->setAutoFillBackground(false);
  scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  cardLay->addWidget(scroll, 1);
}
}
