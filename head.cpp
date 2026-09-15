#include "head.h"

#include <QLabel>

#include "statevis.h"
#include "statuslabel.h"
#include "utils.h"

namespace deepvaccontrol {
Head::Head(ResHolder *resHolder, QWidget *parent)
    : QWidget{parent}, m_resHolder{resHolder}, m_accessLevel{resHolder->users->getCurrentAccessLevel()} {
  initBaseGui();
  setupGuiAndLogic();
}

void Head::setupGuiAndLogic() {
  if (!m_resHolder->states) {
    m_resHolder->logger->stream(Logger::Level::ERR) << "m_resHolder.states is empty";
    return;
  }

  if (!m_resHolder->tcpGate) {
    m_resHolder->logger->stream(Logger::Level::ERR) << "m_resHolder.tcpGate is empty";
    return;
  }

  auto states = m_resHolder->states;

  //    // Положения в компановщике
  //    QQueue<QPair<int, int>> order;
  //    order << QPair<int, int>(1,0) << QPair<int, int>(1,1) << QPair<int, int>(1,2);

  // Имена переменных и описание
  QList<QPair<QString, QString>> st;
  // clang-format off
    st << QPair<QString, QString>("temp", tr("Температура, °C"))
       << QPair<QString, QString>("hum", tr("Относительная влажность, %"))
       << QPair<QString, QString>("pres", tr("Давление, мм рт. ст."))
       << QPair<QString, QString>("lum", tr("Освещённость, лм"))
       << QPair<QString, QString>("solar_en", tr("Солнечная радиация"));
  // clang-format on

  // Контейнер объектов
  QVector<QWidget *> visVector;
  double             guiSF{3.0};
  m_visLay->addStretch();
  for (auto const &st_name : st) {
    if (states->contains(st_name.first)) {
      // if (st_name.first == "lum") {
      //   auto vis = new StateVisLum(st_name.second, this, guiSF);
      //   vis->setVars(states->getSysValue(st_name.first));
      //   m_visLay->addWidget(vis);
      //   visVector << vis;
      // } else if (st_name.first == "solar_en") {
      //   auto vis = new StateVisSolar(st_name.second, this, guiSF);
      //   vis->setVars(states->getSysValue("solar_en"));
      //   m_visLay->addWidget(vis);
      //   visVector << vis;
      // } else if (st_name.first == "pres") {
      //   auto vis = new StateVisSi(st_name.second,
      //                             states->getSysValue(st_name.first),
      //                             states->getSysValue(st_name.first + "_ref"),
      //                             states->getSysValue(st_name.first + "_u"),
      //                             guiSF,
      //                             this);
      //   m_visLay->addWidget(vis);
      //   visVector << vis;
      // } else {
      auto vis = new StateVis(st_name.second,
                              states->getSysValue(st_name.first),
                              states->getSysValue(st_name.first + "_ref"),
                              states->getSysValue(st_name.first + "_u"),
                              guiSF,
                              this);
      m_visLay->addWidget(vis);
      visVector << vis;
    }
  }
  m_visLay->addStretch();

  // setup logic

  connect(states, &SystemValueList::ValuesUpdated, this, &Head::updateGui);
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &Head::updateGui); // todo double gui updates

  connect(m_stopBtn, &StopButton::confirmPressed, this, [this]() {
    m_resHolder->tcpGate->sendTextCmd("stop");
    m_resHolder->actions->addAction(ActionManager::ActionType::PLC, tr("Работа камеры остановлена пользователем"));
  });

  connect(m_nextIntervalBtn, &QPushButton::clicked, this, [this]() {
    m_resHolder->tcpGate->sendTextCmd("next");
  });

  connect(m_resHolder->users, &AccessManager::userSelected, this, &Head::handleAccessChange);
}

void Head::handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel) {
  if (newLevel == m_accessLevel) {
    return;
  }

  m_accessLevel = newLevel;
  updateGui();
}

void Head::initBaseGui() {
  m_mainLay = new QVBoxLayout(this);
  m_mainLay->setContentsMargins(0, 0, 0, 0);

  QHBoxLayout *topLay = new QHBoxLayout();

  m_visLay = new QHBoxLayout();

  m_stopBtn = new StopButton(this);

  m_curWorkTimeLabel = new QLabel(this);
  m_curWorkTimeLabel->setVisible(false);
  m_curWorkTimeLabel->setFont(QFont("MS Shell Dlg 2", 8 * (1 + (3 - 1) * 0.25) * utils::ssf()));
  m_intervalLabel = new QLabel(this);
  m_intervalLabel->setVisible(false);
  m_intervalLabel->setFont(QFont("MS Shell Dlg 2", 8 * (1 + (3 - 1) * 0.25) * utils::ssf()));
  m_cycleLabel = new QLabel(this);
  m_cycleLabel->setVisible(false);
  m_cycleLabel->setFont(QFont("MS Shell Dlg 2", 8 * (1 + (3 - 1) * 0.25) * utils::ssf()));
  m_nextIntervalBtn = new QPushButton(tr("Следующий шаг"), this);
  m_nextIntervalBtn->setFixedSize(120 * utils::ssf(), 40 * utils::ssf());
  m_nextIntervalBtn->setVisible(false);

  m_mainLay->addLayout(topLay);
  m_mainLay->addLayout(m_visLay);
  m_mainLay->addWidget(m_curWorkTimeLabel);
  m_mainLay->addWidget(m_intervalLabel);
  m_mainLay->addWidget(m_cycleLabel);
  m_mainLay->addWidget(m_nextIntervalBtn);
  m_mainLay->addStretch();
  m_mainLay->setAlignment(m_curWorkTimeLabel, Qt::AlignHCenter);
  m_mainLay->setAlignment(m_intervalLabel, Qt::AlignHCenter);
  m_mainLay->setAlignment(m_cycleLabel, Qt::AlignHCenter);
  m_mainLay->setAlignment(m_nextIntervalBtn, Qt::AlignHCenter);
  topLay->addWidget(m_stopBtn);
  topLay->addWidget(new StatusLabel(m_resHolder, this));
  topLay->addStretch();
}

void Head::updateGui() {
  auto const states = m_resHolder->states;

  // qint32 const status_condition{static_cast<qint32>(states->getSysValue("status_condition")->getValue())};

  HardwareStatus::HardwareStatusEnum const hardWorkStatus{m_resHolder->hard_status->getHardwareStatus()};
  HardwareStatus::JobType const            jobType
    = static_cast<HardwareStatus::JobType>(states->getSysValue("work_type")->getValue());

  m_curWorkTimeLabel->setVisible(HardwareStatus::HardwareStatusEnum::WORK == hardWorkStatus);
  m_stopBtn->setEnabled((HardwareStatus::HardwareStatusEnum::WORK == hardWorkStatus)
                        && (AccessManager::AccessLevel::SPECTATOR_0 != m_accessLevel));

  quint32 setPointWork{0U};

  if (setPointWork > 0) {
    m_curWorkTimeLabel->setText(tr("Общее время испытания: ")
                                + utils::timeFromSec((long)states->getSysValue("t_total")->getValue())
                                + tr("\nВремя работы по уставке: ") + utils::timeFromSec(setPointWork, true));
  } else {
    m_curWorkTimeLabel->setText(
      tr("Общее время испытания: ") + utils::timeFromSec((long)states->getSysValue("t_total")->getValue(), true));
  }

  bool const isProgramJob{HardwareStatus::JobType::PROGRAM == jobType};
  m_intervalLabel->setVisible(isProgramJob);

  if (isProgramJob) {
    qint32 const intervalCur{static_cast<qint32>(states->getSysValue("interval")->getValue())};
    qint32 const intervalCount{static_cast<qint32>(states->getSysValue("intervals_count")->getValue())};
    qint32 const cycleCur{static_cast<qint32>(states->getSysValue("cycle_cur")->getValue())};
    qint32 const cycleCount{static_cast<qint32>(states->getSysValue("cycle_total")->getValue())};

    m_intervalLabel->setText(tr("Текущий интервал: ") + QString("%1 из %2").arg(intervalCur).arg(intervalCount));
    bool nextIntervalEnable = (intervalCur < intervalCount) || (cycleCur < cycleCount);
    m_nextIntervalBtn->setVisible(nextIntervalEnable);

    m_cycleLabel->setVisible(cycleCount > 1);

    if (m_cycleLabel->isVisible()) {
      m_cycleLabel->setText(tr("Текущий цикл: ") + QString("%1 из %2").arg(cycleCur).arg(cycleCount));
    }
  } else {
    m_cycleLabel->setVisible(false);
    m_nextIntervalBtn->setVisible(false);
  }
}
} // namespace deepvaccontrol
