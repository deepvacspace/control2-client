#include "experimentlibrary.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <algorithm>
#include <cmath>

#include "runstore.h"
#include "utils.h"

namespace deepvaccontrol {

namespace {
// Gaps shorter than this are never considered as experiment boundaries,
// so the per-file summary only has to remember the longer ones.
constexpr double MIN_BREAK_GAP_SEC{60.0};
// Shorter stretches (application restarts, connection blips) are not listed.
constexpr double MIN_RUN_DURATION_SEC{60.0};
constexpr qint64 MIN_RUN_SAMPLES{10};
constexpr int    PARSED_CACHE_SIZE{4};

QVector<QColor> const FALLBACK_COLORS{QColor("#2563eb"),
                                      QColor("#f97316"),
                                      QColor("#16a34a"),
                                      QColor("#e11d48"),
                                      QColor("#9333ea"),
                                      QColor("#0d9488"),
                                      QColor("#ca8a04"),
                                      QColor("#475569"),
                                      QColor("#db2777"),
                                      QColor("#0284c7")};

qint64 fileModified(QString const &path) {
  return QFileInfo(path).lastModified().toMSecsSinceEpoch();
}

ExperimentLibrary::FileSummary summarizeFile(QString const &path) {
  ExperimentLibrary::FileSummary summary;
  summary.path     = path;
  summary.size     = QFileInfo(path).size();
  summary.modified = fileModified(path);

  QVector<double>          times;
  QVector<QVector<double>> states;
  if (!ExperimentLibrary::readIntervalFile(path, times, states)) {
    return summary;
  }

  qint64 valid{0};
  double lastTime{0.0};
  bool   pendingTerminator{false};
  for (qint64 i{0}; i < times.size(); ++i) {
    bool nanRow{true};
    for (auto const &channel : std::as_const(states)) {
      if ((i < channel.size()) && !std::isnan(channel[i])) {
        nanRow = false;
        break;
      }
    }

    if (nanRow) {
      pendingTerminator = valid > 0;
      continue;
    }

    double const time{times[i]};
    if (0 == valid) {
      summary.first = time;
    } else if (pendingTerminator || ((time - lastTime) >= MIN_BREAK_GAP_SEC)) {
      summary.breaks.push_back({lastTime, time, valid, pendingTerminator});
    }
    pendingTerminator = false;
    lastTime          = time;
    ++valid;
  }

  summary.last               = lastTime;
  summary.valid              = valid;
  summary.endsWithTerminator = pendingTerminator;
  return summary;
}
} // namespace

ExperimentLibrary::ExperimentLibrary(QString const               &intervalsDir,
                                     QString const               &actionsFile,
                                     std::function<QVariantList()> statesProvider,
                                     QObject                     *parent)
    : QObject{parent}
    , m_intervalsDir{intervalsDir}
    , m_actionsFile{actionsFile}
    , m_statesProvider{std::move(statesProvider)} {
}

ExperimentLibrary::~ExperimentLibrary() {
  // The scan thread only touches its own copies, but it must not outlive us.
  for (auto *thread : findChildren<QThread *>()) {
    thread->wait();
  }
}

void ExperimentLibrary::refresh() {
  if (m_scanning) {
    m_rescanRequested = true;
    return;
  }
  m_scanning        = true;
  m_rescanRequested = false;
  emit scanStarted();

  QDir const        dir(m_intervalsDir);
  QStringList const fileNames = dir.entryList({"*.dat"}, QDir::Files, QDir::Name);
  QStringList       paths;
  for (QString const &name : fileNames) {
    paths.push_back(dir.filePath(name));
  }

  auto cache  = std::make_shared<QHash<QString, FileSummary>>(m_summaryCache);
  auto result = std::make_shared<QVector<FileSummary>>();

  QThread *thread = QThread::create([paths, cache, result]() {
    QHash<QString, FileSummary> updated;
    for (QString const &path : paths) {
      QFileInfo const info(path);
      auto            cached = cache->constFind(path);
      FileSummary     summary;
      if ((cached != cache->constEnd()) && (cached->size == info.size())
          && (cached->modified == info.lastModified().toMSecsSinceEpoch())) {
        summary = *cached;
      } else {
        summary = summarizeFile(path);
      }
      updated.insert(path, summary);
      if (summary.valid > 0) {
        result->push_back(summary);
      }
    }
    *cache = std::move(updated);
    std::sort(result->begin(), result->end(), [](FileSummary const &a, FileSummary const &b) {
      return a.first < b.first;
    });
  });
  thread->setParent(this);

  connect(thread, &QThread::finished, this, [this, thread, cache, result]() {
    m_summaryCache = std::move(*cache);
    m_files        = std::move(*result);
    m_scanning     = false;
    thread->deleteLater();
    applySegmentation();
    if (m_rescanRequested) {
      refresh();
    }
  });
  thread->start(QThread::LowPriority);
}

void ExperimentLibrary::setGapThresholdSec(double sec) {
  sec = std::max(MIN_BREAK_GAP_SEC, sec);
  if (qFuzzyCompare(sec, m_gapThresholdSec)) {
    return;
  }
  m_gapThresholdSec = sec;
  if (!m_scanning) {
    applySegmentation();
  }
}

void ExperimentLibrary::applySegmentation() {
  m_runs = segment(m_files);

  m_storedRuns.clear();
  for (storage::StoredRun const &stored : storage::RunStore::list()) {
    ExperimentRun run;
    run.key      = QStringLiteral("db:%1").arg(stored.key);
    run.storeKey = stored.key;
    run.title    = stored.id;
    run.start    = stored.startEpoch;
    run.end      = stored.endEpoch;
    run.samples  = stored.samples;
    run.chamber  = stored.chamber;
    if (std::isnan(run.start)) {
      // runs written by Deepvac Insight only carry text times
      run.start = QDateTime::fromString(stored.startTime.chopped(4), "yyyy-MM-dd HH:mm:ss").toSecsSinceEpoch();
      run.end   = run.start + (std::isnan(stored.durationS) ? 0.0 : stored.durationS);
    }
    m_storedRuns.push_back(run);
  }
  emit runsChanged();
}

QVector<ExperimentRun> ExperimentLibrary::segment(QVector<FileSummary> const &files) const {
  QVector<ExperimentRun> segments;
  QVector<bool>          terminated;

  auto pushSegment = [&](ExperimentRun segment, bool endsWithTerminator, bool firstInFile) {
    if (firstInFile && !segments.isEmpty() && !terminated.last()
        && ((segment.start - segments.last().end) <= m_gapThresholdSec)) {
      // continuation of the previous file's last experiment (e.g. across midnight)
      auto &previous = segments.last();
      previous.end   = segment.end;
      previous.samples += segment.samples;
      previous.files.append(segment.files);
      terminated.last() = endsWithTerminator;
      return;
    }
    segments.push_back(segment);
    terminated.push_back(endsWithTerminator);
  };

  for (FileSummary const &file : files) {
    if (file.valid <= 0) {
      continue;
    }

    qint64 validBefore{0};
    double segmentStart{file.first};
    bool   firstInFile{true};
    for (FileBreak const &brk : file.breaks) {
      bool const active{brk.forced || ((brk.tAfter - brk.tBefore) > m_gapThresholdSec)};
      if (!active) {
        continue;
      }
      ExperimentRun run;
      run.start   = segmentStart;
      run.end     = brk.tBefore;
      run.samples = brk.validBefore - validBefore;
      run.files   = {file.path};
      pushSegment(run, brk.forced, firstInFile);
      firstInFile  = false;
      validBefore  = brk.validBefore;
      segmentStart = brk.tAfter;
    }

    ExperimentRun run;
    run.start   = segmentStart;
    run.end     = file.last;
    run.samples = file.valid - validBefore;
    run.files   = {file.path};
    pushSegment(run, file.endsWithTerminator, firstInFile);
  }

  QVector<ExperimentRun> result;
  for (ExperimentRun &run : segments) {
    if ((run.duration() < MIN_RUN_DURATION_SEC) || (run.samples < MIN_RUN_SAMPLES)) {
      continue;
    }
    run.key   = QStringLiteral("run-%1").arg(static_cast<qint64>(run.start));
    run.title = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.start)).toString("yyyy-MM-dd HH:mm");
    result.push_back(run);
  }

  // newest first
  std::reverse(result.begin(), result.end());
  return result;
}

std::optional<ExperimentRun> ExperimentLibrary::findRun(QString const &key) const {
  for (QVector<ExperimentRun> const *list : {&m_runs, &m_storedRuns}) {
    for (ExperimentRun const &run : *list) {
      if (run.key == key) {
        return run;
      }
    }
  }
  return std::nullopt;
}

ExperimentRun ExperimentLibrary::makeRangeRun(double start, double end) const {
  ExperimentRun run;
  run.custom = true;
  run.start  = std::min(start, end);
  run.end    = std::max(start, end);
  run.key    = QStringLiteral("range-%1-%2").arg(static_cast<qint64>(run.start)).arg(static_cast<qint64>(run.end));

  QDateTime const from = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.start));
  QDateTime const to   = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(run.end));
  run.title = (from.date() == to.date())
                ? QStringLiteral("%1 %2–%3").arg(from.toString("yyyy-MM-dd"), from.toString("HH:mm"), to.toString("HH:mm"))
                : QStringLiteral("%1 – %2").arg(from.toString("yyyy-MM-dd HH:mm"), to.toString("yyyy-MM-dd HH:mm"));

  for (QDate day = from.date(); day <= to.date(); day = day.addDays(1)) {
    QString const path
      = QDir(m_intervalsDir).filePath(utils::calculateFileName(QDateTime(day, QTime(12, 0)).toSecsSinceEpoch()));
    if (QFile::exists(path)) {
      run.files.push_back(path);
    }
  }
  return run;
}

std::shared_ptr<ExperimentLibrary::ParsedFile const> ExperimentLibrary::parsedFile(QString const &path) {
  qint64 const modified{fileModified(path)};
  for (int i{0}; i < m_parsedCache.size(); ++i) {
    auto const cached = m_parsedCache[i];
    if ((cached->path == path) && (cached->modified == modified)) {
      m_parsedCache.move(i, 0);
      return cached;
    }
  }

  auto parsed      = std::make_shared<ParsedFile>();
  parsed->path     = path;
  parsed->modified = modified;
  if (!readIntervalFile(path, parsed->times, parsed->states)) {
    return nullptr;
  }

  m_parsedCache.prepend(parsed);
  while (m_parsedCache.size() > PARSED_CACHE_SIZE) {
    m_parsedCache.removeLast();
  }
  return parsed;
}

std::shared_ptr<ExperimentData const> ExperimentLibrary::load(ExperimentRun const &run, QString *error) {
  auto data = std::make_shared<ExperimentData>();
  data->run = run;

  if (!run.storeKey.isEmpty()) {
    storage::RunSamples samples;
    if (!storage::RunStore::loadSamples(run.storeKey, samples, error)) {
      return nullptr;
    }
    data->times        = std::move(samples.times);
    data->states       = std::move(samples.values);
    data->channelNames = std::move(samples.columns);
    data->run.samples  = data->times.size();
    data->run.start    = data->times.first();
    data->run.end      = data->times.last();
    return data;
  }

  if (run.files.isEmpty()) {
    if (error) {
      *error = tr("No recorded data in the selected period");
    }
    return nullptr;
  }

  for (QString const &path : run.files) {
    auto const file = parsedFile(path);
    if (!file) {
      if (error) {
        *error = tr("Failed to read %1").arg(QFileInfo(path).fileName());
      }
      return nullptr;
    }

    auto const   first = std::lower_bound(file->times.cbegin(), file->times.cend(), run.start);
    auto const   last  = std::upper_bound(first, file->times.cend(), run.end);
    qint64 const begin{first - file->times.cbegin()};
    qint64 const count{last - first};
    if (count <= 0) {
      continue;
    }

    qint64 const offset{data->times.size()};
    if (data->states.size() < file->states.size()) {
      // newer files may contain more states; pad the older part with NaN
      for (int c = data->states.size(); c < file->states.size(); ++c) {
        data->states.push_back(QVector<double>(offset, qQNaN()));
      }
    }

    data->times.append(file->times.mid(begin, count));
    for (int c{0}; c < data->states.size(); ++c) {
      if (c < file->states.size()) {
        QVector<double> const &source = file->states[c];
        for (qint64 i{begin}; i < begin + count; ++i) {
          data->states[c].push_back(i < source.size() ? source[i] : qQNaN());
        }
      } else {
        data->states[c].append(QVector<double>(count, qQNaN()));
      }
    }
  }

  if (data->times.isEmpty()) {
    if (error) {
      *error = tr("No recorded data in the selected period");
    }
    return nullptr;
  }

  data->run.samples = data->times.size();
  if (run.custom) {
    // shrink custom ranges to the recorded data
    data->run.start = data->times.first();
    data->run.end   = data->times.last();
  }
  return data;
}

QVector<ExperimentEvent> ExperimentLibrary::events(double start, double end) {
  qint64 const modified{QFileInfo::exists(m_actionsFile) ? fileModified(m_actionsFile) : -1};
  if (modified != m_eventsModified) {
    m_eventsModified = modified;
    m_events.clear();

    QFile file(m_actionsFile);
    if (file.open(QIODevice::ReadOnly)) {
      QVariantList list;
      QDataStream  in(&file);
      in >> list;
      if (QDataStream::Ok == in.status()) {
        for (QVariant const &item : std::as_const(list)) {
          QVariantMap const map = item.toMap();
          m_events.push_back({map["date"].toDouble(), map["type"].toInt(), map["msg"].toString()});
        }
        std::sort(m_events.begin(), m_events.end(), [](ExperimentEvent const &a, ExperimentEvent const &b) {
          return a.time < b.time;
        });
      }
    }
  }

  QVector<ExperimentEvent> result;
  auto it = std::lower_bound(m_events.cbegin(), m_events.cend(), start, [](ExperimentEvent const &e, double t) {
    return e.time < t;
  });
  for (; (it != m_events.cend()) && (it->time <= end); ++it) {
    result.push_back(*it);
  }
  return result;
}

QVector<ChannelInfo> ExperimentLibrary::channels(ExperimentData const &data) const {
  QVariantList const statesSettings = m_statesProvider ? m_statesProvider() : QVariantList{};
  QHash<QString, int> settingsByName;
  for (int i{0}; i < statesSettings.size(); ++i) {
    settingsByName.insert(statesSettings[i].toMap()["name"].toString(), i);
  }

  QVector<ChannelInfo> result;
  int const            count{static_cast<int>(data.states.size())};
  for (int i{0}; i < count; ++i) {
    ChannelInfo info;
    // interval files follow the settings order; database runs name their channels
    int const settingsIndex{data.channelNames.isEmpty() ? i : settingsByName.value(data.channelNames.value(i), -1)};
    if (!data.channelNames.isEmpty()) {
      info.name = data.channelNames.value(i);
    }
    if ((settingsIndex >= 0) && (settingsIndex < statesSettings.size())) {
      QVariantMap const map = statesSettings[settingsIndex].toMap();
      info.name             = map["name"].toString();
      info.label            = map["label"].toString();
      info.color            = QColor(map["color"].toString());
      info.defaultVisible   = map.contains("color") && map["visible"].toBool();
      info.scientific       = map["notation"].toString() == "scientific";
    }
    if (info.name.isEmpty()) {
      info.name = QStringLiteral("ch%1").arg(i);
    }
    if (info.label.isEmpty()) {
      info.label = info.name;
    }
    if (!info.color.isValid()) {
      info.color = FALLBACK_COLORS[i % FALLBACK_COLORS.size()];
    }
    result.push_back(info);
  }
  return result;
}

bool ExperimentLibrary::readIntervalFile(QString const &path, QVector<double> &times, QVector<QVector<double>> &states) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  // same layout as DataHolder::writeData
  QDataStream in(&file);
  in.setFloatingPointPrecision(QDataStream::DoublePrecision);
  QVariantMap header;
  in >> header;
  in >> times;
  in >> states;
  return QDataStream::Ok == in.status();
}

QString ExperimentLibrary::formatDuration(double sec) {
  qint64 total{static_cast<qint64>(std::llround(std::max(0.0, sec)))};
  qint64 const days{total / 86400};
  total %= 86400;
  QString const hms = QStringLiteral("%1:%2:%3")
                        .arg(total / 3600, 2, 10, QChar('0'))
                        .arg((total % 3600) / 60, 2, 10, QChar('0'))
                        .arg(total % 60, 2, 10, QChar('0'));
  return days > 0 ? QStringLiteral("%1d %2").arg(days).arg(hms) : hms;
}

} // namespace deepvaccontrol
