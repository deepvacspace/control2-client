#include "settingslistwidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "lineeditnum.h"
#include "lineedittime.h"
#include "utils.h"

namespace deepvaccontrol {
SettingsListWidget::SettingsListWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  double const ssf{utils::ssf()};
  auto        *pageLay = new QVBoxLayout(this);
  pageLay->setContentsMargins(0, 8 * ssf, 0, 0);
  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  pageLay->addWidget(card);
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  cardLay->setSpacing(8 * ssf);
  auto *title = new QLabel(tr("Operating parameters"), card);
  title->setObjectName("cardTitle");
  cardLay->addWidget(title);

  // rows in two columns, scrolled when they do not fit
  auto *list    = new QWidget();
  auto *listLay = new QGridLayout(list);
  listLay->setContentsMargins(0, 0, 4 * ssf, 0);
  listLay->setHorizontalSpacing(10 * ssf);
  listLay->setVerticalSpacing(6 * ssf);

  SystemValueList &settingsValueList{resHolder->settings->plcSettingsSystemValueList()};
  QStringList      settingsOrder{settingsValueList.getNames()};

  QStringList const ignoreList{"alarm_cfg_1", "alarm_cfg_2"};
  QFont const       fieldFont = font();
  int               shown{0};
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
    rowLay->setContentsMargins(12 * ssf, 6 * ssf, 10 * ssf, 6 * ssf);
    rowLay->setSpacing(10 * ssf);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(1 * ssf);
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
      timeField->setFixedSize(130 * ssf, 30 * ssf);
      for (LineEditNum *part : timeField->findChildren<LineEditNum *>()) {
        part->setFont(fieldFont);
        part->setFixedHeight(30 * ssf);
      }
      editField = timeField;
    } else {
      auto *numField = new LineEditNum(settingSysValue, row);
      numField->setFont(fieldFont);
      numField->setFixedSize(130 * ssf, 30 * ssf);
      editField = numField;
    }
    rowLay->addWidget(editField);
    auto *unitWidget = new QLabel(unit, row);
    unitWidget->setProperty("muted", true);
    unitWidget->setFixedWidth(40 * ssf);
    rowLay->addWidget(unitWidget);

    listLay->addWidget(row, shown / 2, shown % 2);
    ++shown;
  }
  listLay->setRowStretch((shown + 1) / 2, 1);
  listLay->setColumnStretch(0, 1);
  listLay->setColumnStretch(1, 1);

  auto *scroll = new QScrollArea(card);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(list);
  scroll->viewport()->setAutoFillBackground(false);
  list->setAutoFillBackground(false);
  scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  cardLay->addWidget(scroll, 1);
}
} // namespace deepvaccontrol
