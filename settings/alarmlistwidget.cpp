#include "alarmlistwidget.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include "alarmsinglewidget.h"
#include "systemvalue.h"
#include "utils.h"

namespace deepvaccontrol {
AlarmListWidget::AlarmListWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  double const ssf{utils::ssf()};
  auto        *pageLay = new QVBoxLayout(this);
  pageLay->setContentsMargins(0, 8 * ssf, 0, 0);
  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  pageLay->addWidget(card);
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  cardLay->setSpacing(8 * ssf);

  // header: title; column captions over the input state and the trigger condition
  auto *header = new QHBoxLayout();
  header->setContentsMargins(0, 0, 10 * ssf, 0);
  header->setSpacing(10 * ssf);
  auto *title = new QLabel(tr("Alarms"), card);
  title->setObjectName("cardTitle");
  header->addWidget(title, 1);
  auto caption = [card](QString const &text, int width) {
    auto *label = new QLabel(text.toUpper(), card);
    label->setProperty("muted", true);
    label->setAlignment(Qt::AlignCenter);
    label->setFixedWidth(width);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    label->setFont(font);
    return label;
  };
  header->addWidget(caption(tr("Input"), static_cast<int>(70 * ssf)));
  header->addSpacing(10 * ssf);
  header->addWidget(caption(tr("Trigger condition"), static_cast<int>(240 * ssf)));
  cardLay->addLayout(header);

  auto *mainLay = new QVBoxLayout();
  mainLay->setSpacing(6 * ssf);
  cardLay->addLayout(mainLay);

  QVariantList const alarmLabelsVariantList = resHolder->settings->settingsMap()["alarms"].toList();

  // unused indexes for forced reset
  QSet<quint32> resetList;
  for(quint32 i{0U}; i<32U; ++i) {
    resetList.insert(i);
  }

  auto *alarm_cfg_1 = resHolder->settings->plcSettingsSystemValueList().getSysValue("alarm_cfg_1");
  auto *alarm_cfg_2 = resHolder->settings->plcSettingsSystemValueList().getSysValue("alarm_cfg_2");

  for (auto const &alarmLabelVariant : alarmLabelsVariantList) {
    QVariantMap const alarmLabelVariantMap = alarmLabelVariant.toMap();

    quint32 const index{alarmLabelVariantMap["index"].toUInt()};
    QString const label{alarmLabelVariantMap["label"].toString()};
    QString const description{QString("%1. %2").arg(index + 1).arg(label)};

    auto *alarmWidget = new AlarmSingleWidget(description,
                                              resHolder->states->getSysValue("alarm_in_status"),
                                              alarm_cfg_1,
                                              alarm_cfg_2,
                                              index,
                                              this);
    mainLay->addWidget(alarmWidget);

    resetList.remove(index);
  }

  // forced reset unused
  for(auto &index : resetList) {
    alarm_cfg_1->setBitOfFloat(index, false, /*shadow*/ true);
    alarm_cfg_2->setBitOfFloat(index, false, /*shadow*/ true);
  }
  alarm_cfg_1->notify();
  alarm_cfg_2->notify();

  cardLay->addStretch(1);
}
}
