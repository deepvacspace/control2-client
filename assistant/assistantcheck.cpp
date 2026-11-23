#include "assistantcheck.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <utility>

#include "analysisservices.h"
#include "assistantsession.h"
#include "experimentlibrary.h"

namespace deepvaccontrol::assistant {

namespace {
constexpr int TIMEOUT_MS{300'000}; // a question, the model's start included
constexpr int MAX_SENTENCES{3};    // what the assistant is told to keep to
constexpr int MAX_ITEMS{10};

// sentences and list items of an answer, counted as the assistant's own limit counts them
std::pair<int, int> lengthOf(QString const &answer) {
  static QRegularExpression const listItem(QStringLiteral("^(?:[-*•]\\s|\\d+[.)]\\s|#\\d+\\s*[:—–-]\\s)"));
  int sentences{0};
  int items{0};
  for (QString const &line : answer.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    QString const text = line.trimmed();
    if (text.isEmpty() || text.startsWith(QChar(0x26A0))) { // the warning lines DeepVac adds are not the model's
      continue;
    }
    if (listItem.match(text).hasMatch()) {
      ++items;
      continue;
    }
    int count{0};
    for (qsizetype i{0}; i < text.size(); ++i) {
      QChar const c = text.at(i);
      if ((c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?')) && ((i + 1 == text.size()) || text.at(i + 1).isSpace())) {
        ++count;
      }
    }
    sentences += std::max(count, 1);
  }
  return {sentences, items};
}
}

AssistantCheck::AssistantCheck(AssistantSession *session, AnalysisServices *analysis, QString path, QObject *parent)
    : QObject{parent}, m_session{session}, m_analysis{analysis}, m_path{std::move(path)} {
  m_timeout.setSingleShot(true);
  m_timeout.setInterval(TIMEOUT_MS);
  connect(&m_timeout, &QTimer::timeout, this, [this] {
    if (m_waiting) {
      m_session->stop();
      closeCase(m_partial, true);
    }
  });
  connect(m_session, &AssistantSession::exchange, this, &AssistantCheck::onExchange);
}

void AssistantCheck::start() {
  m_report << QStringLiteral("Assistant check, %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
  if (!m_analysis) {
    m_report << QStringLiteral("No experiment library: nothing asked.");
    write();
    emit finished();
    return;
  }
  m_analysis->ensureScanned();
  begin();
}

void AssistantCheck::begin() {
  if (m_analysis->library()->isScanning()) {
    QTimer::singleShot(300, this, &AssistantCheck::begin);
    return;
  }
  // the two latest experiments of ten minutes or longer: the one "shown", and one to compare it with
  QVector<ExperimentRun> const runs = m_analysis->library()->chronological();
  QVector<int>                 picked; // newest first
  for (int i{static_cast<int>(runs.size()) - 1}; (i >= 0) && (picked.size() < 2); --i) {
    if (runs[i].duration() >= 600.0) {
      picked << i;
    }
  }
  m_report << QStringLiteral("%1 experiments in the list.").arg(runs.size());
  auto reference = [](int index) { return QStringLiteral("#%1").arg(index + 1); };
  auto clock     = [](double t) { return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(t)).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")); };

  m_cases = {{QStringLiteral("Summarize the latest experiment."), {}, false, true},
             {QStringLiteral("What were its main KPIs?")},
             {QStringLiteral("What stages did it have, and which one went worst?")},
             {QStringLiteral("Were there any anomalies?")},
             {QStringLiteral("List the five most recent experiments."), {}, false, true},
             {QStringLiteral("Which experiment had the lowest overshoot?")},
             {QStringLiteral("What is the chamber doing right now?"), {}, false, true},
             {QStringLiteral("What are the temperature PID settings?")},
             {QStringLiteral("What happened today?")},
             {QStringLiteral("How long would it take to go from 20 °C to 60 °C?")},
             {QStringLiteral("Start the chamber at 80 °C."), {}, false, true},
             {QStringLiteral("What is the capital of France?")},
             {QStringLiteral("¿Cuál fue el overshoot del último experimento?")},
             // questions close to the ones answered without an experiment: these are about one
             {QStringLiteral("When did the latest experiment start, and how long did it last?"), {}, false, true},
             {QStringLiteral("Show me the last three experiments.")},
             {QStringLiteral("What is the temperature calibration?")},
             {QStringLiteral("Suggest a better PID for going from 20 °C to 60 °C.")}};
  if (picked.size() == 2) {
    m_cases.push_back({QStringLiteral("Compare experiments %1 and %2.").arg(reference(picked[1]), reference(picked[0])), {}, false, true});
  }
  if (!picked.isEmpty()) {
    ExperimentRun const &run = runs[picked.first()];
    m_view                   = QJsonObject{{"run", reference(picked.first())},
                                           {"duration_s", run.duration()},
                                           {"from_s", 0.0},
                                           {"to_s", run.duration()},
                                           {"from", clock(run.start)},
                                           {"to", clock(run.end)},
                                           {"channels", QJsonArray{QStringLiteral("temp"), QStringLiteral("temp_ref")}}};
    m_report << QStringLiteral("Experiment shown for the panel's questions: %1 \"%2\".").arg(reference(picked.first()), run.title);
    // the panel's quick buttons (their own words), then the chart and the files
    m_cases.push_back({QStringLiteral("Explain this experiment: what it was set to do, how well the temperature followed the setpoint (main "
                                      "KPIs and stages), and the anomalies detected with how serious they are."),
                       QStringLiteral("explain"), true, true});
    m_cases.push_back({QStringLiteral("Compare this experiment with the saved experiments that had the same setpoints: was it better or worse "
                                      "than usual, and which one was best?"),
                       QStringLiteral("similar"), true, true});
    m_cases.push_back({QStringLiteral("How could this experiment have gone better? Compare its main setpoint step with what the digital twin "
                                      "predicts with the AI Advisor's PID."),
                       QStringLiteral("improve"), true, true});
    m_cases.push_back({QStringLiteral("Generate an HTML report of this experiment."), QStringLiteral("report"), true, true});
    m_cases.push_back({QStringLiteral("Zoom to the last hold."), {}, true, true});
    m_cases.push_back({QStringLiteral("How well did the temperature follow the setpoint in this view?"), {}, true});
    m_cases.push_back({QStringLiteral("Export the data of this experiment."), {}, true});
  }
  next();
}

void AssistantCheck::next() {
  ++m_index;
  if (m_index >= m_cases.size()) {
    m_report << QString{}
             << QStringLiteral("Summary: %1 questions · longest answer %2 characters · longer than %3 sentences or %4 list items: %5 · "
                               "with a warning about its values: %6 · errors or timeouts: %7")
                  .arg(m_cases.size())
                  .arg(m_longest)
                  .arg(MAX_SENTENCES)
                  .arg(MAX_ITEMS)
                  .arg(m_tooLong)
                  .arg(m_warned)
                  .arg(m_failed);
    write();
    emit finished();
    return;
  }
  Case const &item = m_cases[m_index];
  if (item.newChat) {
    m_session->newChat();
  }
  m_steps.clear();
  m_partial.clear();
  m_waiting = true;
  m_clock.start();
  m_timeout.start();
  QJsonObject view = item.shown ? m_view : QJsonObject{};
  if (!item.intent.isEmpty()) {
    view.insert("intent", item.intent);
  }
  m_session->ask(item.question, view);
}

void AssistantCheck::onExchange(QString const &status, QString const &answer, bool done) {
  if (!m_waiting) {
    return;
  }
  if (!status.isEmpty() && (m_steps.isEmpty() || (m_steps.last() != status))) {
    m_steps << status;
  }
  if (!answer.isEmpty()) {
    m_partial = answer;
  }
  if (done) {
    closeCase(answer.isEmpty() ? status : answer, false);
  }
}

void AssistantCheck::closeCase(QString const &answer, bool timedOut) {
  m_waiting = false;
  m_timeout.stop();
  Case const &item          = m_cases[m_index];
  auto const [sentences, items] = lengthOf(answer);
  bool const tooLong{(sentences > MAX_SENTENCES) || (items > MAX_ITEMS)};
  bool const warned{answer.contains(QChar(0x26A0))};
  bool const failed{timedOut || answer.startsWith(QLatin1String("Error:")) || answer.trimmed().isEmpty()};
  m_longest = std::max(m_longest, static_cast<int>(answer.size()));
  m_tooLong += tooLong ? 1 : 0;
  m_warned += warned ? 1 : 0;
  m_failed += failed ? 1 : 0;
  m_report << QString{}
           << QStringLiteral("== %1. %2%3%4")
                .arg(m_index + 1)
                .arg(item.question, item.intent.isEmpty() ? QString{} : QStringLiteral("  [button: %1]").arg(item.intent),
                     item.shown ? QStringLiteral("  [experiment shown]") : QString{})
           << QStringLiteral("   steps: %1").arg(m_steps.join(QStringLiteral(" → ")))
           << QStringLiteral("   %1 s · %2 characters · %3 sentences · %4 list items%5%6%7")
                .arg(m_clock.elapsed() / 1000.0, 0, 'f', 1)
                .arg(answer.size())
                .arg(sentences)
                .arg(items)
                .arg(tooLong ? QStringLiteral(" · TOO LONG") : QString{}, warned ? QStringLiteral(" · WARNING ADDED") : QString{},
                     timedOut ? QStringLiteral(" · TIMEOUT") : failed ? QStringLiteral(" · FAILED") : QString{})
           << QStringLiteral("   answer: %1").arg(QString(answer).replace(QLatin1Char('\n'), QStringLiteral("\n           ")));
  write(); // as far as it got, should a later question hang
  QTimer::singleShot(200, this, &AssistantCheck::next);
}

void AssistantCheck::write() const {
  QDir().mkpath(QFileInfo(m_path).absolutePath());
  QSaveFile file(m_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    qWarning() << "assistant-check: cannot write" << m_path;
    return;
  }
  QTextStream out(&file);
  out << m_report.join(QLatin1Char('\n')) << '\n';
  out.flush();
  file.commit();
}

}
