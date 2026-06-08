#include "forceoutlistwidget.h"

#include <QVBoxLayout>

#include "scrollwidget.h"

namespace control2 {
ForceOutListWidget::ForceOutListWidget(
  ResHolder *resHolder, SystemValue *outsSystVal, QVariantList const &forceOutVariantList, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_outsSystVal{outsSystVal} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(0);

  auto *btnLay = new QHBoxLayout();
  btnLay->setContentsMargins(3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf());
  btnLay->setSpacing(3 * utils::ssf());
  mainLay->addLayout(btnLay);

  auto *elementLay = new QVBoxLayout();
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
        index, label, outsSystVal, static_cast<ForceOutSingleWidget::ForceType>(outType), this);
      elementLay->addWidget(item);
      connect(item, &ForceOutSingleWidget::generateRaw, this, &ForceOutListWidget::sendForceOutMsg);
      m_outWidgets.push_back(item);
    }
  }
  elementLay->addStretch();

  auto *elementScrollWidget = new ScrollWidget(true, this);
  elementScrollWidget->setLayout2(elementLay);

  mainLay->addWidget(elementScrollWidget);

  auto *resetAllBtn = new QPushButton(tr("Освободить все выходы"), this);
  auto *setAllBtn   = new QPushButton(tr("Установить все выходы"), this);

  resetAllBtn->setFixedHeight(35 * utils::ssf());
  setAllBtn->setFixedHeight(35 * utils::ssf());

  // mainLay->addStretch();

  btnLay->addWidget(resetAllBtn);
  btnLay->addWidget(setAllBtn);
  btnLay->addStretch();

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

} // namespace control2
