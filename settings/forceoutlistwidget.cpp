#include "forceoutlistwidget.h"
#include "theme.h"

#include <QVBoxLayout>

#include <QFrame>
#include <QLabel>
#include <QScrollArea>

namespace deepvaccontrol {
ForceOutListWidget::ForceOutListWidget(
  ResHolder *resHolder, SystemValue *outsSystVal, QVariantList const &forceOutVariantList, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_outsSystVal{outsSystVal} {
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, theme::dp(16), 0, 0);

  auto *card = new QFrame(this);
  card->setObjectName("pageCard");
  mainLay->addWidget(card);
  auto *cardLay = new QVBoxLayout(card);
  cardLay->setContentsMargins(theme::dp(28), theme::dp(20), theme::dp(28), theme::dp(24));
  cardLay->setSpacing(theme::dp(16));

  // header: title, note, all outputs at once
  auto *header = new QHBoxLayout();
  header->setSpacing(theme::dp(16));
  auto *title = new QLabel(tr("Manual output control"), card);
  title->setObjectName("cardTitle");
  header->addWidget(title);
  header->addStretch();
  auto *resetAllBtn = new QPushButton(tr("Release all outputs"), card);
  resetAllBtn->setProperty("outline", true);
  auto *setAllBtn = new QPushButton(tr("Set all outputs"), card);
  setAllBtn->setObjectName("primaryButton");
  for (QPushButton *button : {resetAllBtn, setAllBtn}) {
    button->setMinimumSize(theme::dp(281), theme::dp(52));
    header->addWidget(button);
  }
  cardLay->addLayout(header);

  // one row per output
  auto *list    = new QWidget();
  auto *listLay = new QVBoxLayout(list);
  listLay->setContentsMargins(0, 0, theme::dp(8), 0);
  listLay->setSpacing(theme::dp(12));
  for (auto const &forceOutVariant : forceOutVariantList) {
    QVariantMap const forceOutVariantMap = forceOutVariant.toMap();

    QString const label = forceOutVariantMap["label"].toString();
    uint const    index = forceOutVariantMap["bit"].toUInt();
    int           outType{0};

    if (forceOutVariantMap.contains("forceOutType")) {
      outType = forceOutVariantMap["forceOutType"].toUInt();
    }

    if (outType > 0) {
      auto *item = new ForceOutSingleWidget(
        index, label, outsSystVal, static_cast<ForceOutSingleWidget::ForceType>(outType), list);
      listLay->addWidget(item);
      connect(item, &ForceOutSingleWidget::generateRaw, this, &ForceOutListWidget::sendForceOutMsg);
      m_outWidgets.push_back(item);
    }
  }
  listLay->addStretch();
  auto *scroll = new QScrollArea(card);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(list);
  list->setAutoFillBackground(false);
  scroll->viewport()->setAutoFillBackground(false);
  scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
  cardLay->addWidget(scroll, 1);

  connect(resetAllBtn, &QPushButton::clicked, this, [this]() {
    m_resHolder->tcpGate->sendTextCmd("reset_force_out");
    for (auto &forceWidget : m_outWidgets) {
      forceWidget->resetSelection();
    }
  });

  connect(setAllBtn, &QPushButton::clicked, this, [this]() {
    QByteArray forceOutData;
    for (auto &forceWidget : m_outWidgets) {
      forceOutData.push_back(forceWidget->getRawData());
      forceWidget->resetSetBtn();
    }

    sendForceOutMsg(m_outWidgets.size(), forceOutData);
  });
}

void ForceOutListWidget::sendForceOutMsg(int count, QByteArray data) {
  QByteArray  sizeData;
  QDataStream dataStream(&sizeData, QIODeviceBase::WriteOnly);
  dataStream.setByteOrder(QDataStream::ByteOrder::LittleEndian);
  dataStream << static_cast<quint8>(count);

  data = sizeData + data;

  m_resHolder->logger->stream(Logger::Level::DEB) << data.toHex();
  m_resHolder->tcpGate->sendGeneral(GateTcp::OutcomeMessageCode::FORCE_OUT, data);
}

}
