#include "assistantengine.h"

#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <utility>
#include <algorithm>
#include <cmath>

#include "llmclient.h"
#include "toolregistry.h"

namespace deepvaccontrol::assistant {

namespace {
constexpr int HISTORY_MESSAGES{24}; // kept besides the system prompt

QString compact(QJsonObject const &object) {
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
} // namespace

AssistantEngine::AssistantEngine(LlmClient *client, ToolRegistry const *registry, QObject *parent)
    : QObject{parent}, m_client{client}, m_registry{registry} {
}

QString AssistantEngine::systemPrompt() const {
  // stable for the whole day, so the server can keep it (and the tools) processed in its cache
  return QStringLiteral(
           "You are the analysis assistant of DeepVac Control, software for thermal vacuum (TVAC) test chambers. "
           "Today is %1. You help engineers understand recorded experiments (the tools call them runs; always say 'experiment' to the user). Always answer in English.\n"
           "How to work:\n"
           "- Get data with tools: get_run_summary (describe a run), compare_runs (any question about two or more "
           "runs), rank_runs (best / worst run, ranking), get_stages / get_kpis (stages), get_anomalies, list_runs, "
           "get_run_details (DUT, purpose, configuration as recorded), get_events (what happened when), "
           "plot_run (show / plot / open), generate_report. If answering needs data you do not have in a card of "
           "this conversation, call a tool first.\n"
           "- Temperatures are always in °C, as in the cards. Never use Kelvin, never convert units, never calculate "
           "numbers yourself.\n"
           "- Experiments: '#N', a title, or 'latest'; 'experiment 105' or 'run 105' is '105'. 'Final stabilization' is stage 'last_hold'.\n"
           "- The user already sees every tool result as a card with its facts and table, right above your answer. "
           "Do not repeat the card.\n"
           "- Answer in at most 3 short sentences: the direct answer to the question, then the most important "
           "observation. Quote at most 3 values, exactly as written in the facts and with their units. Never "
           "calculate new numbers.\n"
           "- You are read-only: you cannot start or stop the chamber or change PID gains, settings or limits; "
           "say that the engineer does this in the control pages.")
    .arg(QDate::currentDate().toString(Qt::ISODate));
}

void AssistantEngine::warmUp() {
  if (m_busy || m_client->busy()) {
    return;
  }
  // processes the system prompt and the tool list once, so the first question only adds its own tokens
  QJsonArray const messages{QJsonObject{{"role", "system"}, {"content", systemPrompt()}}, QJsonObject{{"role", "user"}, {"content", "Ready?"}}};
  m_client->chat(messages, m_registry->openAiTools(), [](LlmReply const &) {}, {}, 1);
}

void AssistantEngine::setMaxToolRounds(int rounds) {
  m_maxRounds = rounds;
}

QStringList AssistantEngine::mentionedRuns(QString const &question) {
  static QRegularExpression const reference(QStringLiteral("(?:#|\\b(?:runs?|corridas?|ensayos?|experiments?|experimentos?)\\s*#?)(\\d+)"),
                                            QRegularExpression::CaseInsensitiveOption);
  // "runs 105 and 108", "#3, #4 y #7": numbers following a run reference
  static QRegularExpression const followUp(QStringLiteral("^\\s*(?:,|and|y|e|vs\\.?|versus|or|o|&)\\s*#?(\\d+)"),
                                           QRegularExpression::CaseInsensitiveOption);
  QStringList runs;
  for (auto it = reference.globalMatch(question); it.hasNext();) {
    QRegularExpressionMatch const match = it.next();
    QString const               first   = QStringLiteral("#") + match.captured(1);
    if (!runs.contains(first)) {
      runs << first;
    }
    qsizetype position{match.capturedEnd()};
    for (QRegularExpressionMatch next = followUp.match(question.mid(position)); next.hasMatch(); next = followUp.match(question.mid(position))) {
      QString const run = QStringLiteral("#") + next.captured(1);
      if (!runs.contains(run)) {
        runs << run;
      }
      position += next.capturedEnd();
    }
  }
  return runs;
}

bool AssistantEngine::usesKelvin(QString const &answer) {
  static QRegularExpression const kelvinUnit(QStringLiteral("\\d\\s?K\\b")); // "298.15 K", not "Kp"
  return kelvinUnit.match(answer).hasMatch() || answer.contains(QStringLiteral("kelvin"), Qt::CaseInsensitive);
}

void AssistantEngine::setGroundingCheck(bool enabled) {
  m_groundingCheck = enabled;
}

QStringList AssistantEngine::ungroundedNumbers(QString const &answer, QString const &evidence) {
  static QRegularExpression const numberPattern(QStringLiteral("[-+]?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?"));
  // numbers that are not measurements: dates, clock times, run / stage references
  static QRegularExpression const ignored(
    QStringLiteral("\\d{4}-\\d{2}-\\d{2}|\\d{1,2}:\\d{2}(?::\\d{2})?|#\\d+|"
                   "\\b(?:runs?|stages?|etapas?|intervals?|intervalos?|steps?|cycles?|ciclos?)\\s*#?\\d+"),
    QRegularExpression::CaseInsensitiveOption);
  static QRegularExpression const commaNumber(QStringLiteral("[-+]?\\d+(?:,\\d+)+"));
  static QRegularExpression const exponent(QStringLiteral("[eE].*"));

  QVector<double> known;
  for (auto it = numberPattern.globalMatch(evidence); it.hasNext();) {
    double const value{std::fabs(it.next().captured().toDouble())};
    known << value;
    if (value >= 60.0) {
      known << value / 60.0 << value / 3600.0; // seconds may be told as minutes / hours
    }
  }
  // a value from the answer is grounded when some result rounds to it
  auto grounded = [&known](QString const &token) {
    double const value{std::fabs(token.toDouble())};
    bool const   integer{!token.contains(QLatin1Char('.')) && !token.contains(QLatin1Char('e'), Qt::CaseInsensitive)};
    if (integer && (value <= 10.0)) {
      return true; // counts, ordinals
    }
    QString mantissa = token;
    mantissa.remove(exponent);
    int const    decimals{mantissa.contains(QLatin1Char('.')) ? static_cast<int>(mantissa.section(QLatin1Char('.'), 1).size()) : 0};
    double const rounding{0.5 * std::pow(10.0, -decimals)};
    return std::any_of(known.cbegin(), known.cend(), [&](double candidate) {
      return std::fabs(candidate - value) <= std::max(rounding, 0.005 * std::fabs(candidate)) + 1e-12;
    });
  };

  QString text = answer;
  text.replace(ignored, QStringLiteral(" "));

  QStringList unknown;
  // "6,301" (thousands) or "0,7" (decimal comma): either reading may be meant
  for (auto it = commaNumber.globalMatch(text); it.hasNext();) {
    QString const token     = it.next().captured();
    QString       thousands = token;
    thousands.remove(QLatin1Char(','));
    QString decimal = token;
    decimal.replace(QLatin1Char(','), QLatin1Char('.'));
    static QRegularExpression const thousandsGroups(QStringLiteral("^[-+]?\\d{1,3}(?:,\\d{3})+$"));
    bool const ok{(thousandsGroups.match(token).hasMatch() && grounded(thousands))
                  || ((token.count(QLatin1Char(',')) == 1) && grounded(decimal))};
    if (!ok && !unknown.contains(token)) {
      unknown << token;
    }
  }
  text.replace(commaNumber, QStringLiteral(" "));

  for (auto it = numberPattern.globalMatch(text); it.hasNext();) {
    QString const token = it.next().captured();
    if (!grounded(token) && !unknown.contains(token)) {
      unknown << token;
    }
  }
  return unknown;
}

void AssistantEngine::setMaxResultChars(int chars) {
  m_maxResultChars = chars;
}

bool AssistantEngine::busy() const {
  return m_busy;
}

void AssistantEngine::reset() {
  cancel();
  m_history = {};
  m_evidence.clear();
}

void AssistantEngine::cancel() {
  if (m_busy) {
    m_cancelled = true;
    m_client->cancel();
  }
}

void AssistantEngine::ask(QString const &question, QString const &focusRun) {
  if (m_busy) {
    emit failed(tr("Still answering the previous question"));
    return;
  }
  QString content = question.trimmed();
  if (!focusRun.isEmpty()) {
    content = tr("%1\n(Experiment selected in the app: %2. The question may name another experiment.)").arg(content, focusRun);
  }
  // routing aid for small models: a question naming several runs is answered with one compare_runs call
  QStringList const runs = mentionedRuns(question);
  if (runs.size() >= 2) {
    content += QStringLiteral("\n(Hint: answer with one compare_runs call for runs %1.)").arg(runs.join(QStringLiteral(", ")));
  }
  static QRegularExpression const showVerbs(
    QStringLiteral("\\b(?:show|plot|display|open|graph|chart|muestra|mostrar|muéstrame|grafica|graficar|abre|abrir|ver)\\b"),
    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  if (showVerbs.match(question).hasMatch()) {
    content += QStringLiteral("\n(Hint: the user wants to see it: call plot_run.)");
  }
  m_history.push_back(QJsonObject{{"role", "user"}, {"content", content}});
  trimHistory();
  m_round     = 0;
  m_cancelled = false;
  m_corrected = false; // evidence is kept: follow-up questions are checked against everything fetched so far
  m_busy      = true;
  emit busyChanged(true);
  request();
}

void AssistantEngine::trimHistory() {
  // drop the oldest messages, but never start with a tool result or an assistant tool call
  while (m_history.size() > HISTORY_MESSAGES) {
    m_history.removeFirst();
    while (!m_history.isEmpty() && (m_history.first().toObject().value("role").toString() != QLatin1String("user"))) {
      m_history.removeFirst();
    }
  }
}

void AssistantEngine::request() {
  QJsonArray messages{QJsonObject{{"role", "system"}, {"content", systemPrompt()}}};
  for (QJsonValue const &message : std::as_const(m_history)) {
    messages.push_back(message);
  }
  // after the last allowed round the model must answer with what it has
  QJsonArray const tools = (m_round < m_maxRounds) ? m_registry->openAiTools() : QJsonArray{};

  m_client->chat(
    messages, tools,
    [this](LlmReply const &reply) {
    if (m_cancelled) {
      finish();
      emit failed(tr("Cancelled"));
      return;
    }
    if (!reply.ok) {
      finish();
      emit failed(reply.error);
      return;
    }
    m_history.push_back(reply.message);

    if (reply.toolCalls.isEmpty()) {
      QString answer = reply.content.isEmpty() ? tr("(no answer)") : reply.content;
      if (m_groundingCheck) {
        QStringList const unknown = ungroundedNumbers(reply.content, m_evidence);
        bool const        kelvin{usesKelvin(reply.content)};
        if ((!unknown.isEmpty() || kelvin) && !m_corrected) {
          // one corrective round: the model is told what was wrong and answers again
          m_corrected = true;
          QStringList problems;
          if (!unknown.isEmpty()) {
            problems << QStringLiteral("these values are in no tool result: %1").arg(unknown.join(QStringLiteral(", ")));
          }
          if (kelvin) {
            problems << QStringLiteral("temperatures must be in °C, never Kelvin");
          }
          m_history.push_back(QJsonObject{
            {"role", "user"},
            {"content", QStringLiteral("Correction: %1. Answer the previous question again in at most 3 sentences, using only "
                                       "values exactly as written in the cards (°C), without converting or calculating. If you "
                                       "need data you do not have, call a tool.")
                          .arg(problems.join(QStringLiteral("; ")))}});
          emit answerRetracted();
          // an answer with invented numbers means the model skipped the data: this time it must call a tool
          m_requireTool = (m_round < m_maxRounds);
          request();
          return;
        }
        if (!unknown.isEmpty()) {
          answer += QStringLiteral("\n\n")
                    + tr("⚠ Not found in the analysis results: %1. Check these values in Data analysis.").arg(unknown.join(QStringLiteral(", ")));
        }
        if (kelvin) {
          answer += QStringLiteral("\n\n") + tr("⚠ DeepVac reports temperatures in °C; values in Kelvin did not come from the analysis.");
        }
      }
      finish();
      emit answered(answer);
      return;
    }

    ++m_round;
    for (LlmToolCall const &call : reply.toolCalls) {
      emit toolCalled(call.name, compact(call.arguments));
      QJsonObject result = call.argumentsError.isEmpty() ? m_registry->call(call.name, call.arguments)
                                                         : QJsonObject{{"error", call.argumentsError}};
      bool const ok{!result.contains("error")};
      // the model reads the card DeepVac wrote (facts with units, table), not the raw data
      QJsonObject const card = result.value("card").toObject();
      QString           text = compact(ok && !card.isEmpty() ? card : result);
      if (text.size() > m_maxResultChars) {
        text = text.left(m_maxResultChars) + QStringLiteral("…(truncated)");
      }
      if (ok && !card.isEmpty()) {
        emit toolCard(call.name, card);
      }
      emit toolFinished(call.name, ok, ok ? tr("%1 characters of results").arg(text.size()) : result.value("error").toString());
      m_history.push_back(QJsonObject{{"role", "tool"}, {"tool_call_id", call.id}, {"name", call.name}, {"content", text}});
      m_evidence += compact(result) + QLatin1Char('\n');
    }
    request();
    },
    [this](QString const &text) { emit partialAnswer(text); }, 0, std::exchange(m_requireTool, false));
}

void AssistantEngine::finish() {
  m_busy = false;
  emit busyChanged(false);
}

} // namespace deepvaccontrol::assistant
