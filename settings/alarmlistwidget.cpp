#include "alarmlistwidget.h"

#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

#include "alarmsinglewidget.h"
#include "systemvalue.h"
#include "utils.h"

namespace control2 {
AlarmListWidget::AlarmListWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent} {
  auto *mainLay = new QVBoxLayout(this);
  auto *label0  = new QLabel(tr("Условие срабатывания"), this);

  auto *headerLay = new QHBoxLayout();
  headerLay->addWidget(label0);
  headerLay->setAlignment(label0, Qt::AlignRight);

  auto *headerCommentBox = new QFrame();
  headerCommentBox->setMinimumWidth(200 * utils::ssf());
  headerLay->addWidget(headerCommentBox);
  headerLay->setStretch(0, 1);
  mainLay->addLayout(headerLay);

  auto *headerCommentLay = new QGridLayout(headerCommentBox);
  headerCommentLay->setContentsMargins(11 * utils::ssf(), 0, 11, 0 * utils::ssf());
  headerCommentLay->addWidget(new QLabel(tr("замыкание"), this), 1, 0, 1, 2);
  headerCommentLay->addWidget(new QLabel(tr("размыкание"), this), 0, 1, 1, 3);

  auto *label1 = new QLabel(tr("выкл"), this);
  headerCommentLay->addWidget(label1, 1, 3, 1, 1);
  headerCommentLay->setAlignment(label1, Qt::AlignRight);
  
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

  mainLay->addStretch(0);
}
} // namespace control2
