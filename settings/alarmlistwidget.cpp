#include "alarmlistwidget.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include "alarmsinglewidget.h"
#include "systemvalue.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
AlarmListWidget::AlarmListWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  double const ssf{utils::ssf()};
  auto        *pageLay = new QVBoxLayout(this);
  pageLay->setContentsMargins(0, theme::dp(16), 0, 0);
  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  pageLay->addWidget(card);
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(theme::dp(28), theme::dp(20), theme::dp(28), theme::dp(24));
  cardLay->setSpacing(theme::dp(16));

  // header: title; column captions over the input state and the trigger condition
  auto *header = new QHBoxLayout();
  header->setContentsMargins(0, 0, theme::dp(20), 0);
  header->setSpacing(theme::dp(20));
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
  header->addSpacing(theme::dp(20));
  header->addWidget(caption(tr("Trigger condition"), static_cast<int>(240 * ssf)));
  cardLay->addLayout(header);

  auto *mainLay = new QVBoxLayout();
  mainLay->setSpacing(theme::dp(12));
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
