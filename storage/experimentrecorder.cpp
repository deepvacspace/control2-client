#include "experimentrecorder.h"

#include <QDateTime>
#include <QDir>
#include <QThread>
#include <cmath>

#include "constants.h"
#include "experimentlibrary.h"
#include "resholder.h"
#include "utils.h"

namespace deepvaccontrol {

using storage::RunStore;

ExperimentRecorder::ExperimentRecorder(ResHolder *resHolder, QObject *parent) : QObject{parent}, m_resHolder{resHolder} {
  connect(m_resHolder->hard_status, &HardwareStatus::statusChanged, this, &ExperimentRecorder::onStatusChanged);
  // experiments saved before KPIs were computed get them now
  QThread *backfill = QThread::create([] {
    RunStore::backfillKpis();
  });
  connect(backfill, &QThread::finished, backfill, &QObject::deleteLater);
  backfill->start(QThread::LowestPriority);

  if (HardwareStatus::WORK == m_resHolder->hard_status->getHardwareStatus()) {
    // started before this app: record from now on
    m_jobStart = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    m_jobLabel = tr("Chamber job");
  }
}

void ExperimentRecorder::setNextJob(QString const &label, QString const &testProfile) {
  m_nextLabel       = label;
  m_nextTestProfile = testProfile;
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

void ExperimentRecorder::onStatusChanged(HardwareStatus::HardwareStatusEnum newStatus,
                                         HardwareStatus::HardwareStatusEnum oldStatus) {
  if ((HardwareStatus::WORK == newStatus) && std::isnan(m_jobStart)) {
    m_jobStart       = static_cast<double>(QDateTime::currentSecsSinceEpoch());
    m_jobLabel       = m_nextLabel.isEmpty() ? tr("Chamber job") : m_nextLabel;
    m_jobTestProfile = m_nextTestProfile;
    m_nextLabel.clear();
    m_nextTestProfile.clear();
  } else if ((HardwareStatus::WORK == oldStatus) && (HardwareStatus::WORK != newStatus)) {
    finishJob(HardwareStatus::ALARM == newStatus);
  }
}

void ExperimentRecorder::finishJob(bool alarm) {
  double const start{m_jobStart};
  double const end{static_cast<double>(QDateTime::currentSecsSinceEpoch())};
  m_jobStart = qQNaN();
  if (std::isnan(start) || ((end - start) < MIN_RUN_DURATION_SEC)) {
    return;
  }

  RunStore::NewRun run;
  QString          error;
  if (!collect(start, end, run, &error)) {
    emit saveFailed(error);
    return;
  }
  run.name = QStringLiteral("%1 %2%3")
               .arg(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(start)).toString("yyyy-MM-dd HH:mm"),
                    m_jobLabel,
                    alarm ? tr(" (alarm)") : QString{});
  run.testProfile = m_jobTestProfile;
  saveAsync(run);
}

bool ExperimentRecorder::collect(double start, double end, RunStore::NewRun &out, QString *error) const {
  out.chamber = chamberName();
  out.runUid  = RunStore::makeRunUid(chamberRef(), start, end);
  out.columns = m_resHolder->states->getNames();
  out.values  = QVector<QVector<double>>(out.columns.size());
  out.times.clear();

  auto const live = m_resHolder->dataManager ? m_resHolder->dataManager->getIntervalToAdd() : nullptr;

  QDate const firstDay = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(start)).date();
  QDate const lastDay  = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(end)).date();
  for (QDate day = firstDay; day <= lastDay; day = day.addDays(1)) {
    double const            key{utils::calculateKey(QDateTime(day, QTime(12, 0)).toSecsSinceEpoch())};
    QVector<double>          times;
    QVector<QVector<double>> states;
    if (live && (live->getIntervalKey() == key)) {
      times  = live->getTimes();
      states = live->getStates();
    } else {
      QString const path = QDir(deepvaccontrol::INTERVALS_DIR).filePath(utils::calculateFileName(static_cast<quint64>(key)));
      if (!ExperimentLibrary::readIntervalFile(path, times, states)) {
        continue;
      }
    }

    for (qsizetype i{0}; i < times.size(); ++i) {
      if ((times[i] < start) || (times[i] > end)) {
        continue;
      }
      bool allNaN{true};
      for (auto const &channel : std::as_const(states)) {
        if ((i < channel.size()) && !std::isnan(channel[i])) {
          allNaN = false;
          break;
        }
      }
      if (allNaN) {
        continue; // application stop marker
      }
      out.times.push_back(times[i]);
      for (int c{0}; c < out.columns.size(); ++c) {
        out.values[c].push_back((c < states.size()) && (i < states[c].size()) ? states[c][i] : qQNaN());
      }
    }
  }

  if (out.times.isEmpty()) {
    if (error) {
      *error = tr("No recorded data between %1 and %2")
                 .arg(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(start)).toString("yyyy-MM-dd HH:mm"),
                      QDateTime::fromSecsSinceEpoch(static_cast<qint64>(end)).toString("yyyy-MM-dd HH:mm"));
    }
    return false;
  }
  return true;
}

void ExperimentRecorder::saveAsync(RunStore::NewRun run) {
  auto          result = std::make_shared<QPair<QString, QString>>(); // key, error
  QString const name   = run.name;
  QThread      *thread = QThread::create([run = std::move(run), result]() {
    QString error;
    result->first  = RunStore::save(run, &error);
    result->second = error;
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
