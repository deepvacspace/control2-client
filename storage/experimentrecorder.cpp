#include "experimentrecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QThread>
#include <cmath>

#include "accessmanager.h"
#include "actionmanager.h"
#include "constants.h"
#include "gatetcp.h"
#include "resholder.h"
#include "settingsmanager.h"
#include "systemvaluelist.h"
#include "utils.h"

namespace deepvaccontrol {

using storage::RunStore;

namespace {
double nowSeconds() {
  return static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}

constexpr double PENDING_JOB_MAX_AGE_SEC{600.0}; // a job announced longer ago than this did not start
constexpr int    JOURNAL_PERIOD_MS{5000};       // samples of the running job reach the database this often

// "band 3: Kp 20 -> 30" for every gain that differs between two PID tables
QStringList pidTableChanges(QJsonArray const &before, QJsonArray const &after, QJsonArray &details) {
  QStringList changes;
  for (qsizetype loop{0}; loop < after.size(); ++loop) {
    QJsonObject const oldLoop = before.at(loop).toObject();
    QJsonObject const newLoop = after.at(loop).toObject();
    QJsonArray        bands;
    for (qsizetype band{0}; band < newLoop.value("p").toArray().size(); ++band) {
      QJsonObject entry;
      for (char const *gain : {"p", "i", "d"}) {
        double const from = oldLoop.value(QLatin1String(gain)).toArray().at(band).toDouble(qQNaN());
        double const to   = newLoop.value(QLatin1String(gain)).toArray().at(band).toDouble(qQNaN());
        if (!(from == to)) {
          entry.insert(QStringLiteral("k%1").arg(QLatin1String(gain)), QJsonArray{from, to});
          changes << QStringLiteral("band %1: K%2 %3 → %4").arg(band).arg(QLatin1String(gain)).arg(from).arg(to);
        }
      }
      if (!entry.isEmpty()) {
        entry.insert("band", static_cast<int>(band));
        bands.push_back(entry);
      }
    }
    if (!bands.isEmpty()) {
      details.push_back(QJsonObject{{"loop", newLoop.value("name")}, {"bands", bands}});
    }
  }
  return changes;
}
} // namespace

ExperimentRecorder::ExperimentRecorder(ResHolder *resHolder, QObject *parent) : QObject{parent}, m_resHolder{resHolder} {
  m_journalTimer = new QTimer(this);
  m_journalTimer->setInterval(JOURNAL_PERIOD_MS);
  connect(m_journalTimer, &QTimer::timeout, this, &ExperimentRecorder::flushJournal);
  m_journalTimer->start();
  recoverInterrupted();
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &ExperimentRecorder::onStatusChanged);
  // experiments saved before KPIs / manifests existed get them now
  QThread *backfill = QThread::create([] {
    RunStore::backfillKpis();
  });
  connect(backfill, &QThread::finished, backfill, &QObject::deleteLater);
  backfill->start(QThread::LowestPriority);

  // live events of the running job
  if (m_resHolder->actions) {
    connect(m_resHolder->actions, &ActionManager::added, this, [this](QVariantList const &added, bool) {
      for (QVariant const &entry : added) {
        QVariantMap const action = entry.toMap();
        auto const        type   = static_cast<ActionManager::ActionType>(action.value("type").toUInt());
        char const *eventType = type == ActionManager::ActionType::PLC   ? core::eventtype::PLC_COMMAND
                                : type == ActionManager::ActionType::ERR ? core::eventtype::APP_ERROR
                                                                         : core::eventtype::OPERATOR_ACTION;
        addEvent(eventType, action.value("msg").toString());
      }
    });
  }
  if (m_resHolder->tcpGate) {
    connect(m_resHolder->tcpGate, &GateTcp::alarmReceived, this, [this](quint32 code) {
      QMap<quint32, QString> const labels = m_resHolder->settings->getAlarmsMap();
      QStringList                  names;
      QJsonArray                   bits;
      for (quint32 bit{0}; bit < 32; ++bit) {
        if ((code >> bit) & 1U) {
          names << labels.value(bit, tr("Unknown alarm %1").arg(bit));
          bits.push_back(static_cast<int>(bit));
        }
      }
      addEvent(core::eventtype::ALARM_RECEIVED, QStringLiteral("PLC alarm: %1").arg(names.join(QStringLiteral("; "))),
               QJsonObject{{"code", static_cast<qint64>(code)}, {"bits", bits}, {"labels", QJsonArray::fromStringList(names)}});
    });
  }
  if (m_resHolder->users) {
    connect(m_resHolder->users, &AccessManager::userSelected, this, [this](QString const &name, AccessManager::AccessLevel level) {
      addEvent(core::eventtype::USER_CHANGE, QStringLiteral("User %1 logged in").arg(name),
               QJsonObject{{"user", name}, {"access_level", static_cast<int>(level)}});
    });
  }
  connect(m_resHolder->settings, &SettingsManager::plcSettingsChanged, this, &ExperimentRecorder::onPlcSettingsChanged);
  m_lastPids = QJsonValue::fromVariant(m_resHolder->settings->settingsMap().value("plc_settings").toMap().value("pids"));

  if (HardwareStatus::WORK == m_resHolder->hard_status->getHardwareStatus()) {
    beginJob(tr("started before this application; recorded from now on"));
  }
}

void ExperimentRecorder::setNextJob(JobInfo const &job) {
  m_pendingJob           = job;
  m_pendingConfiguration = configurationSnapshot(job); // the configuration the job is started with
  m_pendingAt            = nowSeconds();
  m_hasPending           = true;
}

void ExperimentRecorder::setNextJob(QString const &label, QString const &testProfile) {
  setNextJob(JobInfo{testProfile.isEmpty() ? QStringLiteral("job") : QStringLiteral("test_profile"), label, testProfile, {}, {}});
}

void ExperimentRecorder::setNextRunMetadata(core::RunMetadata const &metadata) {
  m_nextMetadata = metadata;
  emit nextRunMetadataChanged();
}

core::RunMetadata ExperimentRecorder::nextRunMetadata() const {
  return m_nextMetadata;
}

QJsonObject ExperimentRecorder::configurationSnapshot(JobInfo const &job) const {
  QVariantMap const settings = m_resHolder->settings->settingsMap();
  QJsonObject       jobJson{{"kind", job.kind}, {"label", job.label}, {"test_profile", job.testProfile}};
  if (!job.program.isEmpty()) {
    jobJson.insert("program", QJsonObject::fromVariantMap(job.program));
  }
  if (!job.details.isEmpty()) {
    jobJson.insert("details", job.details);
  }
  QJsonObject user;
  if (m_resHolder->users) {
    user = QJsonObject{{"name", m_resHolder->users->getCurrentUserName()},
                       {"access_level", static_cast<int>(m_resHolder->users->getCurrentAccessLevel())}};
  }
  return QJsonObject{{"captured_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                     {"captured_at_execution", true},
                     {"job", jobJson},
                     {"plc_settings", QJsonObject::fromVariantMap(settings.value("plc_settings").toMap())},
                     {"control_params", QJsonArray::fromVariantList(settings.value("control_params").toList())},
                     {"alarms", QJsonArray::fromVariantList(settings.value("alarms").toList())},
                     {"app", QJsonObject{{"name", QCoreApplication::applicationName()},
                                         {"version", QCoreApplication::applicationVersion()},
                                         {"qt", QString::fromLatin1(qVersion())},
                                         {"built", QStringLiteral(__DATE__ " " __TIME__)}}},
                     {"user", user},
                     {"chamber", QJsonObject{{"name", chamberName()}, {"ref", chamberRef()}}},
                     {"ai_advisor_before_start", m_resHolder->aiAdvisorEnabled}};
}

QString ExperimentRecorder::chamberRef() const {
  QVariantMap const tcpServer = m_resHolder->settings->settingsMap()["tcp_server"].toMap();
  return QStringLiteral("%1:%2").arg(tcpServer["server_ip"].toString().trimmed().toLower()).arg(tcpServer["server_port"].toUInt());
}

QString ExperimentRecorder::chamberName() const {
  QString const ref = chamberRef();
  for (QVariant const &entry : m_resHolder->settings->settingsMap()["chambers"].toList()) {
    QVariantMap const chamber = entry.toMap();
    if (QStringLiteral("%1:%2").arg(chamber["host"].toString().trimmed().toLower()).arg(chamber["port"].toUInt()) == ref) {
      return chamber["name"].toString();
    }
  }
  return ref;
}

bool ExperimentRecorder::pressureControlled() const {
  if (!jobActive()) {
    return false;
  }
  // a parameter is set in an interval when its bit (its place in control_params) is in the interval's flags
  QStringList order;
  for (QVariant const &param : m_resHolder->settings->settingsMap().value("control_params").toList()) {
    order << param.toMap().value("name").toString();
  }
  qsizetype const tempBit{order.indexOf("temp")};
  qsizetype const presBit{order.indexOf("pres")};
  if (presBit < 0) {
    return false;
  }
  bool pressure{false};
  bool temperature{false};
  for (QVariant const &entry : m_job.info.program.value("intervals").toList()) {
    quint32 const flags{entry.toMap().value("flags").toUInt()};
    pressure    = pressure || ((flags >> presBit) & 1U);
    temperature = temperature || ((tempBit >= 0) && ((flags >> tempBit) & 1U));
  }
  return pressure && !temperature;
}

bool ExperimentRecorder::jobActive() const {
  return !std::isnan(m_job.start);
}

QString ExperimentRecorder::runningJobKind() const {
  return jobActive() ? m_job.info.kind : QString{};
}

QVariantMap ExperimentRecorder::runningProgram() const {
  return jobActive() ? m_job.info.program : QVariantMap{};
}

double ExperimentRecorder::runningJobStart() const {
  return m_job.start;
}

void ExperimentRecorder::addEvent(char const *type, QString const &message, QJsonObject const &data) {
  if (jobActive()) {
    m_job.events.push_back(core::RunEvent{nowSeconds(), QString::fromLatin1(type), QStringLiteral("app"), message, data});
  }
}

void ExperimentRecorder::onPlcSettingsChanged() {
  QJsonValue const pids = QJsonValue::fromVariant(m_resHolder->settings->settingsMap().value("plc_settings").toMap().value("pids"));
  if (pids == m_lastPids) {
    return;
  }
  QJsonArray        details;
  QStringList const changes = pidTableChanges(m_lastPids.toArray(), pids.toArray(), details);
  m_lastPids                = pids;
  if (!changes.isEmpty()) {
    QString message = QStringLiteral("PID table written: %1").arg(changes.mid(0, 6).join(QStringLiteral(", ")));
    if (changes.size() > 6) {
      message += QStringLiteral(" (+%1 more)").arg(changes.size() - 6);
    }
    addEvent(core::eventtype::PID_TABLE_WRITTEN, message, QJsonObject{{"loops", details}});
  }
}

void ExperimentRecorder::beginJob(QString const &note) {
  m_job       = Job{};
  m_job.start = nowSeconds();
  bool const announced{m_hasPending && (m_job.start - m_pendingAt <= PENDING_JOB_MAX_AGE_SEC)};
  if (announced) {
    m_job.info          = m_pendingJob;
    m_job.configuration = m_pendingConfiguration;
  } else {
    // started from the PLC panel or before this application: the configuration as found now
    m_job.info          = JobInfo{QStringLiteral("external"), tr("Chamber job"), {}, {}, {}};
    m_job.configuration = configurationSnapshot(m_job.info);
    m_job.configuration.insert("note", note.isEmpty() ? tr("job not started from this application") : note);
  }
  m_hasPending   = false;
  m_job.metadata = m_nextMetadata;
  if (m_job.metadata.operatorName.isEmpty() && m_resHolder->users) {
    m_job.metadata.operatorName = m_resHolder->users->getCurrentUserName();
  }
  if (m_job.info.label.isEmpty()) {
    m_job.info.label = tr("Chamber job");
  }
  // the samples of the job go to the experiment database directly, journaled while it runs
  m_job.columns   = m_resHolder->states->getNames();
  m_job.values    = QVector<QVector<double>>(m_job.columns.size());
  m_job.journalId = QStringLiteral("job-%1").arg(static_cast<qint64>(m_job.start * 1000.0));
  core::RunManifest startManifest;
  startManifest.metadata      = m_job.metadata;
  startManifest.configuration = m_job.configuration;
  startManifest.channels      = core::channelMetadata(m_resHolder->settings->settingsMap().value("states").toList(), m_job.columns);
  RunStore::journalBegin(m_job.journalId, m_job.start, m_job.columns,
                         QJsonObject{{"label", m_job.info.label},
                                     {"test_profile", m_job.info.testProfile},
                                     {"chamber", chamberName()},
                                     {"chamber_ref", chamberRef()},
                                     {"manifest", startManifest.toJson()}});
  addEvent(core::eventtype::JOB_START, QStringLiteral("Job started: %1").arg(m_job.info.label),
           QJsonObject{{"kind", m_job.info.kind}, {"label", m_job.info.label}, {"test_profile", m_job.info.testProfile}});
}

void ExperimentRecorder::onStatusChanged(HardwareStatus::HardwareStatusEnum newStatus,
                                         HardwareStatus::HardwareStatusEnum oldStatus) {
  if ((HardwareStatus::WORK == newStatus) && !jobActive()) {
    beginJob({});
  } else if ((HardwareStatus::WORK == oldStatus) && (HardwareStatus::WORK != newStatus)) {
    finishJob(newStatus);
  }
}

void ExperimentRecorder::finishJob(HardwareStatus::HardwareStatusEnum endStatus) {
  bool const    alarm{HardwareStatus::ALARM == endStatus};
  QString const outcome = alarm                                  ? QStringLiteral("alarm")
                          : (HardwareStatus::FINISH == endStatus) ? QStringLiteral("finished")
                                                                  : QStringLiteral("stopped");
  addEvent(core::eventtype::JOB_END, QStringLiteral("Job %1").arg(outcome), QJsonObject{{"outcome", outcome}});
  flushJournal();
  Job const job = m_job;
  m_job         = Job{};
  double const start{job.start};
  double const end{nowSeconds()};
  if (std::isnan(start) || ((end - start) < MIN_RUN_DURATION_SEC)) {
    RunStore::journalEnd(job.journalId);
    return;
  }

  if (job.times.isEmpty()) {
    RunStore::journalEnd(job.journalId);
    emit saveFailed(tr("No samples were received between %1 and %2")
                      .arg(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(start)).toString("yyyy-MM-dd HH:mm"),
                           QDateTime::fromSecsSinceEpoch(static_cast<qint64>(end)).toString("yyyy-MM-dd HH:mm")));
    return;
  }
  RunStore::NewRun run;
  run.chamber = chamberName();
  run.runUid  = RunStore::makeRunUid(chamberRef(), start, end);
  run.columns = job.columns;
  run.times   = job.times;
  run.values  = job.values;
  // channel definitions (unit, quantity, role) as configured when recorded
  run.manifest.channels = core::channelMetadata(m_resHolder->settings->settingsMap().value("states").toList(), run.columns);
  run.name = QStringLiteral("%1 %2%3")
               .arg(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(start)).toString("yyyy-MM-dd HH:mm"),
                    job.info.label,
                    alarm ? tr(" (alarm)") : QString{});
  run.testProfile = job.info.testProfile;

  // the self-describing part, captured while the job ran
  run.manifest.runUid        = run.runUid;
  run.manifest.name          = run.name;
  run.manifest.chamber       = run.chamber;
  run.manifest.chamberRef    = chamberRef();
  run.manifest.appName       = QCoreApplication::applicationName();
  run.manifest.appVersion    = QCoreApplication::applicationVersion();
  run.manifest.metadata      = job.metadata;
  run.manifest.configuration = job.configuration;
  run.manifest.events        = job.events;
  saveAsync(run, job.journalId);
}

void ExperimentRecorder::addSample(double epochSec, QVector<double> const &values) {
  if (!jobActive() || (values.size() != m_job.columns.size())) {
    return;
  }
  if (std::all_of(values.cbegin(), values.cend(), [](double v) { return std::isnan(v); })) {
    return;
  }
  m_job.times.push_back(epochSec);
  for (qsizetype c{0}; c < values.size(); ++c) {
    m_job.values[c].push_back(values[c]);
  }
  m_job.newTimes.push_back(epochSec);
  m_job.newRows.push_back(values);
}

void ExperimentRecorder::flushJournal() {
  if (!jobActive() || m_job.newTimes.isEmpty()) {
    return;
  }
  if (RunStore::journalAppend(m_job.journalId, m_job.newTimes, m_job.newRows)) {
    m_job.newTimes.clear();
    m_job.newRows.clear();
  }
}

void ExperimentRecorder::recoverInterrupted() {
  for (storage::JournalJob const &pending : RunStore::journalPending()) {
    if (pending.times.size() < 2 || ((pending.times.last() - pending.times.first()) < MIN_RUN_DURATION_SEC)) {
      RunStore::journalEnd(pending.id);
      continue;
    }
    RunStore::NewRun run;
    double const     start{pending.times.first()};
    double const     end{pending.times.last()};
    QString const    label = pending.info.value("label").toString(tr("Chamber job"));
    run.name               = QStringLiteral("%1 %2%3")
                 .arg(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(start)).toString("yyyy-MM-dd HH:mm"), label, tr(" (interrupted)"));
    run.testProfile        = pending.info.value("test_profile").toString();
    run.chamber            = pending.info.value("chamber").toString();
    run.runUid             = RunStore::makeRunUid(pending.info.value("chamber_ref").toString(), start, end);
    run.columns            = pending.columns;
    run.times              = pending.times;
    run.values             = pending.values;
    run.manifest           = core::RunManifest::fromJson(pending.info.value("manifest").toObject());
    run.manifest.runUid    = run.runUid;
    run.manifest.name      = run.name;
    run.manifest.chamber   = run.chamber;
    run.manifest.chamberRef = pending.info.value("chamber_ref").toString();
    run.manifest.appName    = QCoreApplication::applicationName();
    run.manifest.appVersion = QCoreApplication::applicationVersion();
    run.manifest.configuration.insert("note", tr("the application stopped while the job ran; saved from the journal at the next start"));
    saveAsync(run, pending.id);
  }
}

void ExperimentRecorder::saveAsync(RunStore::NewRun run, QString const &journalId) {
  auto          result = std::make_shared<QPair<QString, QString>>(); // key, error
  QString const name   = run.name;
  QThread      *thread = QThread::create([run = std::move(run), result, journalId]() {
    QString error;
    result->first  = RunStore::save(run, &error);
    result->second = error;
    if (!result->first.isEmpty() && !journalId.isEmpty()) {
      RunStore::journalEnd(journalId); // saved: the journal is no longer needed (kept on failure, retried at the next start)
    }
  });
  connect(thread, &QThread::finished, this, [this, thread, result, name]() {
    thread->deleteLater();
    if (result->first.isEmpty()) {
      emit saveFailed(result->second);
    } else {
      emit runSaved(result->first, name);
    }
  });
  thread->start(QThread::LowPriority);
}

} // namespace deepvaccontrol
