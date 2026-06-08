#include "statusbar.h"

#include <QEvent>
#include <QLabel>

#include "constants.h"
#include "labelplus.h"

namespace control2 {

QString setValueFormat(float value, int prec) {
  auto out = QString::number(value, 'f', prec);
  if ((out == "nan") || (value < -10e9) || (value > 10e9)) {
    out = "--";
  }
  return "(" + out + ")";
}

StatusBar::StatusBar(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(3 * utils::ssf(), 3 * utils::ssf(), 3 * utils::ssf(), 0);
  mainLay->setSpacing(3 * utils::ssf());

  stateLay = new QHBoxLayout();
  stateLay->setContentsMargins(0, 0, 0, 0);
  stateLay->setSpacing(0);

  m_hardStatus
    = new QLabel(control2::hardwareStatusList()[static_cast<int>(m_resHolder->hard_status->getHardwareStatus())], this);
  mainLay->addWidget(m_hardStatus);
  mainLay->addWidget(makeSeparator());

  mainLay->addLayout(stateLay);
  for (auto const &[name, label] : control2::statusBarStatesList()) {
    if (m_resHolder->states->contains(name)) {
      auto *stateLabel      = new QLabel(QString("%1: ").arg(label), this);
      auto *stateValueLabel = new LabelPlus(m_resHolder->states->getSysValue(name), this);

      auto *ref_state     = m_resHolder->states->getSysValue(name + "_ref");
      auto *stateRefLabel = new LabelPlus(ref_state, &setValueFormat, this);

      if (ref_state->isSciNotation()) {
        stateRefLabel->setFormat(&utils::formatScientificRef);
      }

      stateLay->addWidget(stateLabel);
      stateLay->addWidget(stateValueLabel);
      stateLay->addWidget(stateRefLabel);
      stateLay->addSpacing(3 * utils::ssf());
    }
  }
  mainLay->addStretch();

  // workStatus    = new QLabel("Статус работы", this);
  // usbLabel      = new QLabel(this);
  // opcStatus     = new QLabel("OPC", this);
  // usbImageFrame = makeSeparator();

  // mainLay->addLayout(stateLay);
  mainLay->addStretch();
  // mainLay->addWidget(workStatus);

  auto *userLabel = new QLabel(m_resHolder->users->getCurrentUserName(), this);
  mainLay->addWidget(makeSeparator());
  mainLay->addWidget(userLabel);

  m_appState = new QLabel(control2::APP_STATE_LIST[static_cast<quint32>(resHolder->status->getAppState())], this);
  mainLay->addWidget(makeSeparator());
  mainLay->addWidget(m_appState);

  m_opcState = new QLabel("", this);

  m_usbLabel = new QLabel(this);
  mainLay->addWidget(makeSeparator());
  mainLay->addWidget(m_usbLabel);
  handleUsb(m_resHolder->usb->getAvailableDrives());

  mainLay->addWidget(makeSeparator());
  mainLay->addWidget(m_opcState);

  // mainLay->addWidget(usbImageFrame);
  // mainLay->addWidget(usbLabel);

  auto *timeLabel = new QLabel(this);
  mainLay->addWidget(makeSeparator());
  mainLay->addWidget(timeLabel);

  auto *timer = new QTimer(this);

  last = QDateTime::currentDateTime();
  timeLabel->setText(last.toString("HH:mm:ss"));

  connect(timer, &QTimer::timeout, this, [this, timeLabel]() {
    if (QDateTime::currentDateTime().msecsTo(last) < -1000) { // если прошло больше секунды

      last = QDateTime::currentDateTime();
      timeLabel->setText(last.toString("HH:mm:ss"));
    }
  });

  handleOpcStateCahnged(-2);

  connect(m_resHolder->status, &StatusManager::appStateChanged, this, &StatusBar::handleAppStateCahnged);
  connect(m_resHolder->status, &StatusManager::opcStateChanged, this, &StatusBar::handleOpcStateCahnged);
  connect(m_resHolder->users,
          &AccessManager::userSelected,
          this,
          [this, userLabel](QString const &userName, AccessManager::AccessLevel level) {
            userLabel->setText(userName);
          });
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &StatusBar::handleHardStatusCahnged);
  connect(m_resHolder->usb, &UsbManager::drivesUpdated, this, &StatusBar::handleUsb);

  timer->start(250);
#ifdef QT_DEBUG
  installEventFilter(this);
#endif

  this->setFixedHeight(20 * utils::ssf());
}

QFrame *StatusBar::makeSeparator() {
  auto *fr = new QFrame(this);
  fr->setFrameStyle(QFrame::VLine | QFrame::Sunken);
  fr->setLineWidth(0);
  fr->setMidLineWidth(1);
  return fr;
}

void StatusBar::handleAppStateCahnged(StatusManager::AppState newState) {
  m_appState->setText(control2::APP_STATE_LIST[static_cast<quint32>(newState)]);
}

void StatusBar::handleHardStatusCahnged(HardwareStatus::HardwareStatusEnum const &newStatus,
                                        HardwareStatus::HardwareStatusEnum const &) {
  m_hardStatus->setText(control2::hardwareStatusList()[static_cast<quint32>(newStatus)]);
}

void StatusBar::handleOpcStateCahnged(int newState) {
  QString opcStatusString;
  switch (newState) {
    case 0:
      opcStatusString = "OPC OK";
      break;
    case -1:
      opcStatusString = "OPC ERR";
      break;
    default:
      opcStatusString = "OPC OFF";
      break;
  }
  m_opcState->setText(opcStatusString);
}

void StatusBar::handleUsb(QStringList driveList) {
  if (driveList.isEmpty()) {
    m_usbLabel->setPixmap(QPixmap());
  } else {
    QPixmap usbPix(QPixmap(":/imgs/usb.png").scaledToHeight(9 * utils::ssf()));
    m_usbLabel->setPixmap(usbPix);
  }
}

bool StatusBar::eventFilter(QObject *watched, QEvent *event) {
  if (event->type() == QEvent::MouseButtonRelease) {
    emit mouseReleased();
    return true;
  }
  return QObject::eventFilter(watched, event);
}
} // namespace control2
