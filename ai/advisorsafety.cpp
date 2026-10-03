#include "advisorsafety.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

#include "actionmanager.h"
#include "constants.h"
#include "gatetcp.h"
#include "logger.h"
#include "resholder.h"
#include "settingsmanager.h"
#include "settingstransformer.h"
#include "statusmanager.h"
#include "systemvalue.h"
#include "systemvaluelist.h"

namespace deepvaccontrol::ai {

namespace {
constexpr int CONFIRM_TIMEOUT_MS{5000};
constexpr int MAX_SEND_ATTEMPTS{3};
constexpr int CLOSE_FLUSH_TIMEOUT_MS{1500};

QString number(double value) {
  return QString::number(value, 'g', 6);
}

bool isOnline(ResHolder *resHolder) {
  return resHolder->status && (StatusManager::AppState::ONLINE == resHolder->status->getAppState());
}

// The temperature PID table is complete and every gain is a finite number.
bool validPidTable(QVariantMap const &pidMap) {
  bool          ok{false};
  quint32 const points = pidMap.value("points").toUInt(&ok);
  if (!ok || (0 == points) || pidMap.value("name").toString().isEmpty()) {
    return false;
  }
  for (char const *key : {"p", "i", "d"}) {
    QVariantList const gains = pidMap.value(key).toList();
    if (static_cast<quint32>(gains.size()) != points) {
      return false;
    }
    for (QVariant const &gain : gains) {
      bool         isNumber{false};
      double const value = gain.toDouble(&isNumber);
      if (!isNumber || !std::isfinite(value)) {
        return false;
      }
    }
  }
  return true;
}

void logAction(ResHolder *resHolder, QString const &text) {
  if (resHolder->actions) {
    resHolder->actions->addAction(ActionManager::ActionType::PLC, text);
  }
  if (resHolder->logger) {
    resHolder->logger->stream(Logger::Level::INFO) << text;
  }
}
} // namespace

QString advisorControlDenied(ResHolder *resHolder) {
  if (!resHolder || !resHolder->users) {
    return QObject::tr("User permissions are not available");
  }
  if (resHolder->users->getCurrentAccessLevel() < ADVISOR_CONTROL_LEVEL) {
    return QObject::tr("User '%1' is not allowed to let the AI Advisor change the chamber PID (edit rights required)")
      .arg(resHolder->users->getCurrentUserName());
  }
  return {};
}

QString ModelEnvelope::checkScenario(double startTemp, double targetTemp, double durationS) const {
  if (!std::isfinite(startTemp) || !std::isfinite(targetTemp) || !std::isfinite(durationS)) {
    return QObject::tr("The scenario contains an invalid value");
  }
  if ((targetTemp < tempMin) || (targetTemp > tempMax)) {
    return QObject::tr("Target %1 °C is outside the model's valid range (%2)").arg(number(targetTemp), describe());
  }
  if (!containsTemp(startTemp)) {
    return QObject::tr("Chamber temperature %1 °C is outside the model's valid range (%2)")
      .arg(number(startTemp), describe());
  }
  if (durationS > maxDurationS) {
    return QObject::tr("Duration %1 s exceeds the model's validated horizon of %2 s")
      .arg(number(durationS), number(maxDurationS));
  }
  return {};
}

bool ModelEnvelope::containsTemp(double temp, double margin) const {
  return std::isfinite(temp) && (temp >= tempMin - margin) && (temp <= tempMax + margin);
}

bool ModelEnvelope::containsPid(double kp, double ki, double kd) const {
  auto inside = [](double value, double lo, double hi) { return std::isfinite(value) && (value >= lo) && (value <= hi); };
  return inside(kp, kpMin, kpMax) && inside(ki, kiMin, kiMax) && inside(kd, kdMin, kdMax);
}

QString ModelEnvelope::describe() const {
  return QObject::tr("%1 … %2 °C").arg(number(tempMin), number(tempMax));
}

ManualPidGuard::ManualPidGuard(ResHolder *resHolder, QString const &owner, QObject *parent)
    : QObject{parent}, m_resHolder{resHolder}, m_owner{owner} {
  m_confirmTimer.setSingleShot(true);
  connect(&m_confirmTimer, &QTimer::timeout, this, &ManualPidGuard::onConfirmTimeout);
  if (m_resHolder->status) {
    connect(m_resHolder->status, &StatusManager::appStateChanged, this, &ManualPidGuard::onAppStateChanged);
  }
  if (m_resHolder->tcpGate) {
    connect(m_resHolder->tcpGate, &GateTcp::settingsReceived, this, &ManualPidGuard::onSettingsEcho);
  }
  loadJournal();
}

bool ManualPidGuard::capture() {
  // A restore that hasn't been confirmed means the chamber may still hold
  // advisor gains -- they must never be taken for the manual ones.
  if (m_pending || m_awaitingEcho) {
    return false;
  }
  QVariantMap const pidMap =
    m_resHolder->settings->settingsMap().value("plc_settings").toMap().value("pids").toList().value(0).toMap();
  if (!validPidTable(pidMap)) {
    return false;
  }
  m_snapshot    = pidMap;
  m_armed       = true;
  m_stopJob     = true; // until told otherwise, a crash leaves an advisor job running
  m_fromJournal = false;
  m_reason      = tr("application closed while the AI Advisor was in control");
  writeJournal();
  return true;
}

bool ManualPidGuard::isArmed() const {
  return m_armed;
}

bool ManualPidGuard::isPending() const {
  return m_pending || m_awaitingEcho;
}

bool ManualPidGuard::manualPidForBand(double temp, double &kp, double &ki, double &kd) const {
  if (m_snapshot.isEmpty()) {
    return false;
  }
  int const points = m_snapshot.value("points").toInt();
  int       band{-1};
  SystemValue *idxVal = m_resHolder->states ? m_resHolder->states->getSysValue("temp_pid_idx") : nullptr;
  if (idxVal && std::isfinite(idxVal->getValue())) {
    band = static_cast<int>(std::lround(idxVal->getValue()));
  } else if (std::isfinite(temp)) {
    double const bandMin = m_snapshot.value("min").toDouble();
    double const bandMax = m_snapshot.value("max").toDouble();
    band = static_cast<int>(std::floor((temp - bandMin) / std::max(bandMax - bandMin, 1e-9) * points));
  } else {
    return false;
  }
  band = std::clamp(band, 0, points - 1);
  kp   = m_snapshot.value("p").toList().value(band).toDouble();
  ki   = m_snapshot.value("i").toList().value(band).toDouble();
  kd   = m_snapshot.value("d").toList().value(band).toDouble();
  return std::isfinite(kp) && std::isfinite(ki) && std::isfinite(kd);
}

ManualPidGuard::Result ManualPidGuard::restore(bool stopJob, QString const &reason) {
  bool const outstanding{m_pending || m_awaitingEcho};
  bool const restorePid{m_armed || (outstanding && m_restorePid)};
  bool const stop{stopJob || (outstanding && m_stopJob)};
  if (!restorePid && !stop) {
    return Result::NOTHING;
  }
  m_confirmTimer.stop();
  m_armed        = false;
  m_awaitingEcho = false;
  m_pending      = true;
  m_restorePid   = restorePid;
  m_stopJob      = stop;
  m_reason       = reason;
  m_attempts     = 0;
  m_sentOnce     = false;
  if (m_restorePid) {
    writeJournal();
  }

  if (isOnline(m_resHolder) && send()) {
    return m_restorePid ? Result::RESTORED : Result::NOTHING;
  }
  logAction(m_resHolder, m_restorePid
                           ? tr("AI Advisor: chamber not reachable, manual PID will be restored on reconnection (%1)").arg(reason)
                           : tr("AI Advisor: chamber not reachable, it will be stopped on reconnection (%1)").arg(reason));
  return m_restorePid ? Result::PENDING : Result::NOTHING;
}

void ManualPidGuard::stopJob(QString const &reason) {
  restore(true, reason);
}

void ManualPidGuard::restoreNow(bool stopJob, QString const &reason) {
  if (!m_armed && !m_pending && !m_awaitingEcho && !stopJob) {
    return;
  }
  restore(stopJob, reason);
  bool const flushed{m_resHolder->tcpGate->flush(CLOSE_FLUSH_TIMEOUT_MS)};
  if (flushed && m_awaitingEcho) {
    // No event loop left to wait for the echo -- written out is the best we can know.
    m_confirmTimer.stop();
    finish();
  }
  // otherwise the journal stays and the restore is done on the next start
}

bool ManualPidGuard::send() {
  if (m_restorePid) {
    QMap<QString, float> const flatMap = pidVariantToStringMap(m_snapshot);
    QVariantMap  plcSettings = m_resHolder->settings->settingsMap().value("plc_settings").toMap();
    QVariantList pids        = plcSettings.value("pids").toList();
    if (flatMap.isEmpty() || pids.isEmpty()) {
      return false;
    }
    pids[0]             = m_snapshot;
    plcSettings["pids"] = pids;
    m_resHolder->settings->setPlcSettings(plcSettings);
    if (m_resHolder->status) {
      m_resHolder->status->setSettingsListIdentical(false); // no "settings match" pop-up for the echo
    }
    m_resHolder->tcpGate->sendSettingMap(flatMap);
  }
  if (m_stopJob) {
    m_resHolder->tcpGate->sendTextCmd("stop");
  }
  m_pending = false;

  if (!m_restorePid) {
    logAction(m_resHolder, tr("AI Advisor stopped the chamber (%1)").arg(m_reason));
    finish();
    return true;
  }

  m_resHolder->tcpGate->sendTextCmd("get_settings"); // the reply confirms what the PLC now holds
  ++m_attempts;
  m_sentOnce     = true;
  m_awaitingEcho = true;
  m_confirmTimer.start(CONFIRM_TIMEOUT_MS);
  logAction(m_resHolder, m_stopJob ? tr("AI Advisor: manual PID restored and chamber stopped (%1)").arg(m_reason)
                                   : tr("AI Advisor: manual PID restored (%1)").arg(m_reason));
  if (m_fromJournal) {
    m_fromJournal = false;
    emit recoveredPreviousSession(m_reason, m_journalJobRunning);
  }
  return true;
}

void ManualPidGuard::onAppStateChanged() {
  if (m_pending && isOnline(m_resHolder)) {
    send();
  } else if (m_awaitingEcho && !isOnline(m_resHolder)) {
    // connection lost before the confirmation -- send everything again later
    m_confirmTimer.stop();
    m_awaitingEcho = false;
    m_pending      = true;
  }
}

void ManualPidGuard::onSettingsEcho(QMap<QString, float> const &plcSettings) {
  // a late echo (after the retries ran out) still confirms what was sent
  bool const sent{m_awaitingEcho || (m_pending && m_sentOnce)};
  if (sent && m_restorePid && echoMatches(plcSettings)) {
    m_confirmTimer.stop();
    finish();
    emit restoreConfirmed();
  }
}

bool ManualPidGuard::echoMatches(QMap<QString, float> const &plc) const {
  QMap<QString, float> const expected = pidVariantToStringMap(m_snapshot);
  for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
    auto const found = plc.constFind(it.key());
    if ((found == plc.cend()) || (std::fabs(found.value() - it.value()) > 1e-4f * std::max(1.0f, std::fabs(it.value())))) {
      return false;
    }
  }
  return true;
}

void ManualPidGuard::onConfirmTimeout() {
  if (!m_awaitingEcho) {
    return;
  }
  m_awaitingEcho = false;
  m_pending      = true;
  if (m_attempts < MAX_SEND_ATTEMPTS) {
    if (isOnline(m_resHolder)) {
      send();
    }
    return;
  }
  QString const message =
    tr("The chamber did not confirm the restored manual PID after %1 attempts. Check Settings -> Temperature PID; "
       "the restore will be retried on the next connection.")
      .arg(m_attempts);
  m_attempts = 0;
  logAction(m_resHolder, tr("AI Advisor: manual PID restore not confirmed"));
  emit restoreFailed(message);
}

void ManualPidGuard::finish() {
  m_armed        = false;
  m_pending      = false;
  m_awaitingEcho = false;
  m_stopJob      = false;
  m_restorePid   = false;
  m_sentOnce     = false;
  m_fromJournal  = false;
  m_attempts     = 0;
  removeJournal();
}

QString ManualPidGuard::journalPath() const {
  return QDir(AI_DIR).absoluteFilePath(QStringLiteral("%1_manual_pid.json").arg(m_owner));
}

void ManualPidGuard::writeJournal() const {
  QDir().mkpath(AI_DIR);
  QJsonObject journal;
  journal["owner"]    = m_owner;
  journal["saved_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  journal["reason"]   = m_reason;
  journal["stop_job"] = m_stopJob;
  journal["pid"]      = QJsonObject::fromVariantMap(m_snapshot);
  QSaveFile file(journalPath());
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(journal).toJson());
    file.commit();
  }
}

void ManualPidGuard::removeJournal() const {
  QFile::remove(journalPath());
}

void ManualPidGuard::loadJournal() {
  QFile file(journalPath());
  if (!file.open(QIODevice::ReadOnly)) {
    return;
  }
  QJsonObject const journal = QJsonDocument::fromJson(file.readAll()).object();
  file.close();
  QVariantMap const snapshot = journal.value("pid").toObject().toVariantMap();
  if (!validPidTable(snapshot)) {
    removeJournal();
    return;
  }
  m_snapshot    = snapshot;
  m_pending     = true;
  m_restorePid  = true;
  m_fromJournal       = true;
  m_journalJobRunning = journal.value("stop_job").toBool(true);
  m_stopJob           = false; // restore the gains only -- stopping the job is the user's call
  m_reason      = journal.value("reason").toString(tr("previous session ended with AI Advisor gains on the chamber"));
  logAction(m_resHolder, tr("AI Advisor: previous session left advisor PID on the chamber, manual PID will be restored"));
  if (isOnline(m_resHolder)) {
    send();
  }
}

void notifyRecoveredSession(QWidget *parent, ManualPidGuard *guard, QString const &reason, bool jobMayBeRunning) {
  auto *box = new QMessageBox(parent);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->setIcon(QMessageBox::Warning);
  box->setWindowTitle(QObject::tr("AI Advisor"));
  box->setText(QObject::tr("The application closed unexpectedly while the AI Advisor was controlling the chamber (%1).").arg(reason));
  if (!jobMayBeRunning) {
    box->setInformativeText(QObject::tr("The manual Temperature PID has been restored."));
    box->setStandardButtons(QMessageBox::Ok);
    box->open();
    return;
  }
  box->setInformativeText(QObject::tr("The manual Temperature PID has been restored. The experiment the AI Advisor started "
                                      "may still be running on the chamber. Do you want to stop it?"));
  QPushButton *stop = box->addButton(QObject::tr("Stop the experiment"), QMessageBox::DestructiveRole);
  box->addButton(QObject::tr("Keep it running"), QMessageBox::RejectRole);
  box->setDefaultButton(stop);
  QPointer<ManualPidGuard> guardPtr{guard};
  QObject::connect(box, &QMessageBox::buttonClicked, box, [box, stop, guardPtr](QAbstractButton *button) {
    if ((button == stop) && guardPtr) {
      guardPtr->stopJob(QObject::tr("stopped by the user after an unexpected close"));
    }
  });
  box->open();
}

} // namespace deepvaccontrol::ai
