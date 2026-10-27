#include "programplanrunner.h"

#include <cmath>

#include "actionmanager.h"
#include "advisorsafety.h"
#include "gatetcp.h"
#include "hardwarestatus.h"
#include "resholder.h"
#include "settingsmanager.h"
#include "settingstransformer.h"
#include "statusmanager.h"
#include "systemvalue.h"
#include "systemvaluelist.h"

namespace deepvaccontrol::ai {

namespace {
constexpr int START_TIMEOUT_MS{20'000};

void logAction(ResHolder *resHolder, QString const &text) {
  if (resHolder->actions) {
    resHolder->actions->addAction(ActionManager::ActionType::PLC, text);
  }
}
}

ProgramPlanRunner::ProgramPlanRunner(ResHolder *resHolder, QObject *parent)
    : QObject{parent}, m_resHolder{resHolder}, m_guard{new ManualPidGuard(resHolder, QStringLiteral("program_plan"), this)} {
  m_startTimeout.setSingleShot(true);
  m_startTimeout.setInterval(START_TIMEOUT_MS);
  connect(&m_startTimeout, &QTimer::timeout, this, [this] {
    if (m_active && !m_running) {
      finish(tr("the program did not start"));
    }
  });
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &ProgramPlanRunner::onStatus);
  connect(m_resHolder->states, &SystemValueList::ValuesUpdated, this, &ProgramPlanRunner::onStates);
}

ProgramPlanRunner::~ProgramPlanRunner() {
  if (m_active) {
    m_guard->restoreNow(false, tr("application closed"));
  }
}

bool ProgramPlanRunner::start(QVector<IntervalPlan> const &plan, QString *error) {
  if (m_active) {
    finish(tr("a new program was started"));
  }
  if (!m_guard->capture()) {
    if (error) {
      *error = m_guard->isPending()
                 ? tr("Your own settings from the last AI Advisor plan have not been confirmed by the chamber yet.")
                 : tr("The chamber's temperature control configuration is not available.");
    }
    return false;
  }
  m_plan    = plan;
  m_active  = true;
  m_running = false;
  m_applied = -1;
  logAction(m_resHolder, tr("AI Advisor: program plan started, settings follow the intervals"));
  apply(0);
  m_startTimeout.start();
  return true;
}

void ProgramPlanRunner::abort(QString const &reason) {
  if (m_active) {
    finish(reason);
  }
}

void ProgramPlanRunner::onStatus() {
  if (!m_active) {
    return;
  }
  bool const working{HardwareStatus::HardwareStatusEnum::WORK == m_resHolder->hard_status->getHardwareStatus()};
  if (working) {
    m_running = true;
    m_startTimeout.stop();
    onStates();
  } else if (m_running) {
    finish(tr("program ended"));
  }
}

void ProgramPlanRunner::onStates() {
  if (!m_active || !m_running || !m_resHolder->states->contains("interval")) {
    return;
  }
  double const current = m_resHolder->states->getSysValue("interval")->getValue(); // 1 = the first interval
  if (std::isfinite(current) && (current >= 1.0)) {
    apply(static_cast<int>(std::lround(current)) - 1);
  }
}

// The interval's settings (in every band), or the user's own table for an interval without a recommendation.
void ProgramPlanRunner::apply(int interval) {
  if ((interval == m_applied) || (interval < 0) || (interval >= m_plan.size())) {
    return;
  }
  IntervalPlan const planned = m_plan[interval];
  IntervalPlan const before  = m_plan.value(m_applied);
  m_applied                  = interval;
  bool const same{planned.valid == before.valid
                  && (!planned.valid || ((planned.kp == before.kp) && (planned.ki == before.ki) && (planned.kd == before.kd)))};
  if (same && (interval > 0)) {
    return; // the chamber already has them
  }

  QVariantMap  plcSettings = m_resHolder->settings->settingsMap().value("plc_settings").toMap();
  QVariantList pids        = plcSettings.value("pids").toList();
  if (pids.isEmpty() || !m_resHolder->tcpGate) {
    return;
  }
  QVariantMap pid = m_guard->snapshot();
  if (planned.valid) {
    int const points = pid.value("points").toInt();
    pid["p"]         = QVariantList(points, planned.kp);
    pid["i"]         = QVariantList(points, planned.ki);
    pid["d"]         = QVariantList(points, planned.kd);
  }
  QMap<QString, float> const flat = pidVariantToStringMap(pid);
  if (flat.isEmpty()) {
    return;
  }
  pids[0]             = pid;
  plcSettings["pids"] = pids;
  m_resHolder->settings->setPlcSettings(plcSettings);
  if (m_resHolder->status) {
    m_resHolder->status->setSettingsListIdentical(false); // no "settings match" pop-up for the chamber's echo
  }
  m_resHolder->tcpGate->sendSettingMap(flat);
  // the values stay in the log, for service
  logAction(m_resHolder, planned.valid ? tr("AI Advisor: interval %1, recommended settings applied (Temperature PID P = %2, I = %3, "
                                            "D = %4 in all bands)")
                                           .arg(interval + 1)
                                           .arg(QString::number(planned.kp, 'f', 1), QString::number(planned.ki, 'f', 1),
                                                QString::number(planned.kd, 'f', 1))
                                       : tr("AI Advisor: interval %1, your own settings").arg(interval + 1));
}

void ProgramPlanRunner::finish(QString const &reason) {
  m_active  = false;
  m_running = false;
  m_applied = -1;
  m_startTimeout.stop();
  m_plan.clear();
  m_guard->restore(false, reason);
}

}
