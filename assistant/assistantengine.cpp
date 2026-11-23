#include "assistantengine.h"

#include <QDate>
#include <QSet>
#include <QJsonArray>
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
constexpr int       HISTORY_MESSAGES{24};          // kept besides the system prompt
constexpr qsizetype MAX_EVIDENCE_CHARS{400'000}; // tool results the answers are checked against

constexpr int       MAX_SENTENCES{3};              // of an answer (a list: MAX_ITEMS items)
constexpr int       MAX_ITEMS{10};

// emoji and other pictographs (check marks, pins, rockets, flags): small models end their answers with them, or get
// stuck writing one over and over
bool isPictograph(char32_t c) {
  return ((c >= 0x1F000) && (c <= 0x1FAFF)) || ((c >= 0x2600) && (c <= 0x27BF)) || ((c >= 0x2B00) && (c <= 0x2BFF)) || (c == 0xFE0F)
         || (c == 0x200D) || ((c >= 0xE0020) && (c <= 0xE007F));
}

// the text without them; count: how many there were
QString withoutMarks(QString const &text, int *count = nullptr) {
  QString out;
  out.reserve(text.size());
  int found{0};
  for (qsizetype i{0}; i < text.size(); ++i) {
    QChar const ch = text.at(i);
    char32_t    c{ch.unicode()};
    qsizetype   length{1};
    if (ch.isHighSurrogate() && (i + 1 < text.size()) && text.at(i + 1).isLowSurrogate()) {
      c      = QChar::surrogateToUcs4(ch, text.at(i + 1));
      length = 2;
    }
    if (isPictograph(c)) {
      ++found;
    } else {
      out.append(QStringView(text).mid(i, length));
    }
    i += length - 1;
  }
  if (count) {
    *count = found;
  }
  return out;
}

// the end of the text is one short piece over and over: the model is stuck (not a number's digits, a table's rule)
bool loopsAtEnd(QString const &text) {
  constexpr qsizetype TIMES{8};
  for (qsizetype unit{1}; unit <= 6; ++unit) {
    if (text.size() < unit * TIMES) {
      break;
    }
    QStringView const tail  = QStringView(text).right(unit * TIMES);
    QStringView const piece = tail.left(unit);
    bool const        plain{std::all_of(piece.cbegin(), piece.cend(), [](QChar ch) {
      return ch.isDigit() || ch.isSpace() || (QStringView(u"-=_*|.#~").contains(ch));
    })};
    if (plain) {
      continue;
    }
    bool same{true};
    for (qsizetype k{1}; same && (k < TIMES); ++k) {
      same = tail.mid(k * unit, unit) == piece;
    }
    if (same) {
      return true;
    }
  }
  return false;
}

QString compact(QJsonObject const &object) {
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
}

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
           "get_day_summary (what happened on a day), plot_run (show / plot / open), generate_report, export_data, "
           "find_similar_runs (better or worse than the experiments with the same setpoints), improve_run (how it "
           "could have gone better: a better PID from the digital twin). About the chamber itself: get_live_status "
           "(what it is doing now), get_settings (its PID, calibration and limits now), predict_setpoint (how it "
           "would go to a setpoint), suggest_pid (a better PID for a setpoint). "
           "Never answer from memory or guess: every value, date, "
           "time and duration comes from a tool result of this conversation.\n"
           "- Start, end and duration of an experiment are in get_run_summary and get_run_details; program interval "
           "durations are what was planned, not how long the experiment lasted.\n"
           "- Temperatures are always in °C, as in the cards. Never use Kelvin, never convert units, never calculate "
           "numbers yourself.\n"
           "- Experiments: '#N', a title, or 'latest'; 'experiment 105' or 'run 105' is '105'. 'Final stabilization' is stage 'last_hold'.\n"
           "- The user does not see the tool results: your answer is all they read. Never mention tools, functions, "
           "cards or results; just answer.\n"
           "- Be brief: answer what was asked in one or two short sentences (three at most, each under 25 words), or "
           "in a short list, one short line per item, when several items are asked for. The direct answer first. Stop as soon as the question is answered: no closing remark, no facts that "
           "were not asked for, no sentence saying that something is unknown or not needed. Quote values exactly as "
           "written in the facts, with their units. Never calculate new numbers. For a report or an export, give its "
           "file path.\n"
           "- Write the answer once, in plain text. No emojis or symbols, check marks, self-review, notes in "
           "parentheses about the answer, or remarks about the query.\n"
           "- A question that is not about the chamber, its experiments, its settings or this software: say in one "
           "sentence that you only help with those, and nothing else.\n"
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

bool AssistantEngine::isControlRequest(QString const &question) {
  // an order ("start the chamber at 80 °C", "can you set the setpoint to 60?"), not a question about one ("when did the
  // chamber start heating?"); not what the assistant does do (reports, charts, exports, predictions)
  static QRegularExpression const order(
    QStringLiteral("^\\s*(?:please\\s+|(?:can|could|would|will)\\s+you\\s+(?:please\\s+)?|i\\s+(?:want|need)\\s+(?:you\\s+)?to\\s+|go\\s+ahead\\s+and\\s+)?"
                   "(?:start|stop|abort|pause|resume|run|launch|set|change|apply|write|upload|turn\\s+(?:on|off)|switch\\s+(?:on|off)|vent|"
                   "pump\\s+down|heat|cool)\\b"),
    QRegularExpression::CaseInsensitiveOption);
  static QRegularExpression const ownWork(
    QStringLiteral("\\b(?:reports?|charts?|plots?|graphs?|compar\\w*|analy[sz]\\w*|exports?|summar\\w*|explain\\w*|describ\\w*|"
                   "simulat\\w*|predict\\w*|chat|view|zoom\\w*|lines?|channels?|list\\w*|show\\w*|tell\\w*|check\\w*)\\b"),
    QRegularExpression::CaseInsensitiveOption);
  static QRegularExpression const chamber(
    QStringLiteral("\\b(?:chamber|program|job|test|experiment|run|setpoint|temperature|pressure|pid|gains?|kp|ki|kd|settings?|limits?|"
                   "calibration|heater|heating|cooling|compressor|pump|valve|vacuum|it)\\b|°\\s*C|\\d"),
    QRegularExpression::CaseInsensitiveOption);
  return order.match(question).hasMatch() && !ownWork.match(question).hasMatch() && chamber.match(question).hasMatch();
}

QVector<std::pair<QString, QJsonObject>> AssistantEngine::routeQuestion(QString const &question) {
  auto has = [&question](char const *pattern) {
    return QRegularExpression(QString::fromLatin1(pattern), QRegularExpression::CaseInsensitiveOption).match(question).hasMatch();
  };
  QVector<std::pair<QString, QJsonObject>> calls;
  // about a particular experiment ("its settings", "this experiment"): not the chamber as it is now
  bool const ofExperiment{has("\\b(?:this|that|the|its|their|last|latest|previous)\\s+(?:\\w+\\s+)?(?:experiment|run)\\b|\\bits\\b|\\bthis view\\b")};

  // a prediction: "how long would it take to go from 20 °C to 60 °C?"
  static QRegularExpression const fromTo(QStringLiteral("\\bfrom\\s+([-−]?\\d+(?:[.,]\\d+)?)\\s*(?:°\\s*C?|C|degrees)?\\s+to\\s+([-−]?\\d+(?:[.,]\\d+)?)"),
                                         QRegularExpression::CaseInsensitiveOption);
  static QRegularExpression const toTarget(QStringLiteral("\\b(?:to|for|at|reach)\\s+([-−]?\\d+(?:[.,]\\d+)?)\\s*(?:°\\s*C?|C\\b|degrees)"),
                                           QRegularExpression::CaseInsensitiveOption);
  auto number = [](QString text) { return text.replace(QChar(0x2212), QLatin1Char('-')).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(); };
  bool const wantsPid{has("\\b(?:suggest|recommend|best|better|optimal|optimi[sz]e|tune|tuning)\\b.*\\b(?:pid|gains?|coefficients?)\\b|"
                          "\\b(?:pid|gains?)\\b.*\\b(?:suggest|recommend|best|better|optimal|tune)\\b")};
  // what would happen, not what did ("how long did it take" is about an experiment)
  bool const wantsPrediction{has("\\bhow long (?:would|will|does it take|to)\\b|\\b(?:predict\\w*|simulat\\w*|would it|will it|estimate\\w*|forecast)\\b")};
  if (!ofExperiment && (wantsPid || wantsPrediction)) {
    QJsonObject arguments;
    if (QRegularExpressionMatch const m = fromTo.match(question); m.hasMatch()) {
      arguments.insert("start", number(m.captured(1)));
      arguments.insert("target", number(m.captured(2)));
    } else if (QRegularExpressionMatch const t = toTarget.match(question); t.hasMatch()) {
      arguments.insert("target", number(t.captured(1)));
    }
    if (arguments.contains("target")) {
      calls.push_back({wantsPid ? QStringLiteral("suggest_pid") : QStringLiteral("predict_setpoint"), arguments});
      return calls;
    }
  }
  // the chamber now
  if (has("\\b(?:right now|currently|at the moment|at present|live)\\b|\\bnow\\s*\\?*\\s*$|"
          "\\b(?:is|are|what's)\\s+(?:the\\s+)?(?:chamber|it)\\b.*\\b(?:doing|running|connected|online|idle|busy|heating|cooling|in alarm)\\b|"
          "\\bcurrent\\s+(?:temperature|pressure|setpoint|status|state|step|interval|cycle)\\b|"
          "\\b(?:time|long)\\s+(?:is\\s+)?(?:left|remaining)\\b|\\bwhen\\s+(?:will|does)\\b.*\\b(?:end|finish|be done)\\b|"
          "\\b(?:any|active)\\s+alarms?\\b.*\\b(?:now|active)\\b|\\bstatus of the chamber\\b|\\bchamber(?:'s)? status\\b")) {
    calls.push_back({QStringLiteral("get_live_status"), QJsonObject{}});
    return calls;
  }
  // the chamber's settings (not those an experiment was recorded with)
  if (!ofExperiment && has("\\bsettings?\\b|\\bcalibrat\\w*|\\b(?:setpoint|temperature|pressure)\\s+(?:limits?|range)\\b|\\bconfigured\\b|"
                           "\\b(?:current|present)\\s+(?:pid|gains?)\\b|\\bpid\\s+(?:table|bands?)\\b")) {
    QString const section = has("\\bcalibrat") ? QStringLiteral("calibration")
                            : has("\\b(?:limits?|range)\\b") ? QStringLiteral("limits")
                            : has("\\b(?:pid|gains?|kp|ki|kd|bands?)\\b") ? QStringLiteral("pid")
                                                                          : QStringLiteral("all");
    calls.push_back({QStringLiteral("get_settings"), QJsonObject{{"section", section}}});
    return calls;
  }
  // a day
  static QRegularExpression const isoDate(QStringLiteral("\\b(\\d{4}-\\d{2}-\\d{2})\\b"));
  if (has("\\btoday\\b")) {
    calls.push_back({QStringLiteral("get_day_summary"), QJsonObject{{"date", QDate::currentDate().toString(Qt::ISODate)}}});
    return calls;
  }
  if (has("\\byesterday\\b")) {
    calls.push_back({QStringLiteral("get_day_summary"), QJsonObject{{"date", QDate::currentDate().addDays(-1).toString(Qt::ISODate)}}});
    return calls;
  }
  if (QRegularExpressionMatch const m = isoDate.match(question); m.hasMatch() && has("\\b(?:happen\\w*|that day|on|summary|did|was|were|ran)\\b")) {
    calls.push_back({QStringLiteral("get_day_summary"), QJsonObject{{"date", m.captured(1)}}});
    return calls;
  }
  // the best or worst experiment by a KPI
  // ("which one went worst" is about the stages of the experiment asked about, not a ranking)
  if (has("\\b(?:which|what)\\s+(?:experiment|run)\\b.*\\b(?:best|worst|lowest|highest|least|most|smallest|largest|biggest|fastest|slowest)\\b|"
          "\\b(?:best|worst|rank\\w*)\\b.*\\b(?:experiments?|runs?)\\b|"
          "\\b(?:lowest|highest|smallest|largest|biggest|least|most)\\b.*\\b(?:overshoot|error|mae|settling)\\b")) {
    QString const metric = has("\\bovershoot") ? QStringLiteral("overshoot")
                           : has("\\bsettl|\\bfast|\\bslow") ? QStringLiteral("settling")
                           : (has("\\bmae\\b") && !has("\\btail|\\bend\\b|\\blast\\b")) ? QStringLiteral("mae")
                                                                                         : QStringLiteral("tail_mae");
    calls.push_back({QStringLiteral("rank_runs"), QJsonObject{{"metric", metric}, {"limit", 5}}});
    return calls;
  }
  // a list of experiments: "list the five most recent experiments"
  if (has("\\b(?:list|show me|give me|what are|which are|name)\\b.*\\b(?:experiments|runs)\\b|"
          "\\b(?:last|latest|recent|newest|previous)\\s+(?:\\w+\\s+){0,2}(?:experiments|runs)\\b|\\bhow many (?:experiments|runs)\\b")) {
    static QStringList const words{"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
    static QRegularExpression const digits(QStringLiteral("\\b(\\d{1,2})\\b"));
    int limit{10};
    if (QRegularExpressionMatch const m = digits.match(question); m.hasMatch()) {
      limit = m.captured(1).toInt();
    } else {
      for (int i{0}; i < words.size(); ++i) {
        if (has(QStringLiteral("\\b%1\\b").arg(words[i]).toLatin1().constData())) {
          limit = i + 1;
          break;
        }
      }
    }
    calls.push_back({QStringLiteral("list_runs"), QJsonObject{{"limit", std::clamp(limit, 1, 20)}}});
    return calls;
  }
  return calls;
}

QJsonObject AssistantEngine::chartCommand(QString const &question, QJsonObject const &view) {
  QString const run = view.value("run").toString();
  if (run.isEmpty()) {
    return {};
  }
  auto has = [&question](char const *pattern) {
    return QRegularExpression(QString::fromLatin1(pattern), QRegularExpression::CaseInsensitiveOption).match(question).hasMatch();
  };
  if (!has("\\b(?:zoom|show|go to|jump|focus|display|fit|move to|take me|bring)\\b")) {
    return {};
  }
  double const from{view.value("from_s").toDouble()};
  double const to{view.value("to_s").toDouble()};
  double const duration{view.value("duration_s").toDouble(to)};
  QJsonObject  args{{"run", run}};
  // the whole experiment
  if (has("\\b(?:fit|whole|entire|everything|full (?:run|experiment|range)|reset (?:the )?(?:zoom|view))\\b")) {
    return args;
  }
  // a stage: "the second hold", "last ramp", "stage 3"
  static QRegularExpression const stageNumber(QStringLiteral("\\bstage\\s*(\\d+)\\b"), QRegularExpression::CaseInsensitiveOption);
  if (QRegularExpressionMatch const m = stageNumber.match(question); m.hasMatch()) {
    args.insert("stage", m.captured(1));
    return args;
  }
  static QRegularExpression const stageKind(
    QStringLiteral("\\b(first|second|third|fourth|fifth|sixth|last|final|\\d+(?:st|nd|rd|th))?\\s*(hold|stabili[sz]ation|soak|ramp|setpoint|step)s?\\b"),
    QRegularExpression::CaseInsensitiveOption);
  if (QRegularExpressionMatch const m = stageKind.match(question); m.hasMatch()) {
    static QStringList const ordinals{"first", "second", "third", "fourth", "fifth", "sixth"};
    QString const ordinal = m.captured(1).toLower();
    int           occurrence{1};
    if ((ordinal == QLatin1String("last")) || (ordinal == QLatin1String("final"))) {
      occurrence = -1;
    } else if (ordinals.contains(ordinal)) {
      occurrence = static_cast<int>(ordinals.indexOf(ordinal)) + 1;
    } else if (!ordinal.isEmpty()) {
      occurrence = ordinal.left(ordinal.size() - 2).toInt();
    }
    QString const word = m.captured(2).toLower();
    QString const kind = word.startsWith(QLatin1String("ramp"))                                         ? QStringLiteral("ramp")
                         : (word.startsWith(QLatin1String("setpoint")) || word.startsWith(QLatin1String("step"))) ? QStringLiteral("setpoint")
                                                                                                        : QStringLiteral("hold");
    args.insert("stage", QStringLiteral("%1:%2").arg(kind).arg(occurrence));
    return args;
  }
  // a stretch of time: "the last 2 h", "first 30 min"
  static QRegularExpression const stretch(QStringLiteral("\\b(last|final|first)\\s+(\\d+(?:[.,]\\d+)?)\\s*(h|hours?|min|minutes?|s|seconds?)\\b"),
                                          QRegularExpression::CaseInsensitiveOption);
  if (QRegularExpressionMatch const m = stretch.match(question); m.hasMatch() && (duration > 0)) {
    double const amount{m.captured(2).replace(QLatin1Char(','), QLatin1Char('.')).toDouble()};
    QString const unit = m.captured(3).toLower();
    double const  span{amount * (unit.startsWith(QLatin1Char('h')) ? 3600.0 : (unit.startsWith(QLatin1String("m")) ? 60.0 : 1.0))};
    bool const    first{m.captured(1).toLower() == QLatin1String("first")};
    args.insert("from_s", std::round(first ? 0.0 : std::max(0.0, duration - span)));
    args.insert("to_s", std::round(first ? std::min(duration, span) : duration));
    return args;
  }
  // in or out of the view shown, around its middle
  if ((to > from) && has("\\bzoom (?:in|out)\\b")) {
    double const middle{(from + to) / 2.0};
    double const half{(to - from) / 2.0 * (has("\\bzoom out\\b") ? 2.0 : 0.5)};
    args.insert("from_s", std::round(std::max(0.0, middle - half)));
    args.insert("to_s", std::round((duration > 0) ? std::min(duration, middle + half) : middle + half));
    return args;
  }
  return {};
}

QString AssistantEngine::withoutNoise(QString const &answer) {
  static QRegularExpression const selfReview(
    QStringLiteral("\\s*\\((?=[^()]*\\b(?:final|response|requirements?|constraints?|confirm\\w*|delivered|end of|"
                   "accurate|complete|respected|verified|answer)\\b)[^()]*\\)"),
    QRegularExpression::CaseInsensitiveOption);
  QString text = withoutMarks(answer);
  text.remove(selfReview);
  // a line that is nothing but a remark in parentheses ("(No critical issues were reported.)"): the answer said it
  static QRegularExpression const asideLine(QStringLiteral("^[ \\t]*\\([^()\\n]*\\)[ \\t]*$"), QRegularExpression::MultilineOption);
  text.remove(asideLine);
  // a list of experiments written in one run ("...are:#12 — ... .#11 — ..."): each on its own line, as list items
  static QRegularExpression const gluedItem(QStringLiteral("([.:;])\\s*(#\\d+\\s*[—–-]\\s)"));
  text.replace(gluedItem, QStringLiteral("\\1\n- \\2"));
  return text;
}

QString AssistantEngine::cleanAnswer(QString const &answer) {
  QString const text = withoutNoise(answer);
  // the answer ends where a small model starts looping: a sentence that comes again, one that only restates numbers
  // already given, an echo of its instructions, or more than 4 sentences (a list may have up to 10 items)
  static QRegularExpression const echo(
    QStringLiteral("quote values|quoted exactly|exactly as (?:recorded|written|quoted)|no calculated|never calculate|"
                   "tool results?\\b|the user does not see|as instructed"),
    QRegularExpression::CaseInsensitiveOption);
  static QRegularExpression const numberPattern(QStringLiteral("\\d+(?:[.,]\\d+)?"));
  // a line of a list: "- ...", "2. ...", or an experiment by its number ("#12: ...", "#12 — ...")
  static QRegularExpression const listItem(QStringLiteral("^(?:[-*•]\\s|\\d+[.)]\\s|#\\d+\\s*[:—–-]\\s)"));
  // the "2." of a numbered list is no sentence
  auto listNumber = [](QStringView piece) {
    piece = piece.trimmed();
    return !piece.isEmpty() && (piece.size() <= 3) && std::all_of(piece.cbegin(), piece.cend(), [](QChar ch) { return ch.isDigit(); });
  };
  QSet<QString> seen;
  QSet<QString> numbersSeen;
  int           sentences{0};
  int           items{0};
  qsizetype     start{0};
  qsizetype     kept{0}; // text up to here is the answer
  for (qsizetype i{0}; i <= text.size(); ++i) {
    bool const atEnd{i == text.size()};
    QChar const c = atEnd ? QChar() : text.at(i);
    // a sentence ends at . ! ? followed by a space (not in "1.5", not after a list's number) or at a line break
    bool const number{!atEnd && (c == QLatin1Char('.')) && listNumber(QStringView(text).mid(start, i - start))};
    bool const stop{!atEnd && !number && (c == QLatin1Char('\n')
                               || ((c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?'))
                                   && ((i + 1 == text.size()) || text.at(i + 1).isSpace())))};
    if (!stop && !atEnd) {
      continue;
    }
    qsizetype const end{atEnd ? i : i + 1};
    QString const   piece = text.mid(start, end - start).trimmed();
    bool const      complete{!atEnd || piece.endsWith(QLatin1Char('.')) || piece.endsWith(QLatin1Char('!')) || piece.endsWith(QLatin1Char('?'))};
    start = end;
    if (piece.isEmpty()) {
      kept = end;
      continue;
    }
    // the number of a list's next line, its text still to come while the answer is written ("6."): no sentence yet (it
    // was taken for one that repeats a number already given, and the list was cut there)
    if (atEnd && listNumber(piece.endsWith(QLatin1Char('.')) ? QStringView(piece).chopped(1) : QStringView(piece))) {
      kept = end;
      continue;
    }
    if (echo.match(piece).hasMatch()) {
      break;
    }
    QString key = piece.toLower();
    key.remove(QRegularExpression(QStringLiteral("[^a-z0-9]")));
    if ((key.size() >= 6) && seen.contains(key)) {
      break;
    }
    bool const  item{listItem.match(piece).hasMatch()};
    // one sentence more than allowed has begun (while it is written, or left unfinished): the answer ends before it
    if (!complete && !item && (sentences >= MAX_SENTENCES)) {
      break;
    }
    QStringList numbers;
    for (auto it = numberPattern.globalMatch(piece); it.hasNext();) {
      numbers << it.next().captured();
    }
    bool const restates{complete && !item && (sentences > 0) && !numbers.isEmpty()
                        && std::all_of(numbers.cbegin(), numbers.cend(), [&](QString const &n) { return numbersSeen.contains(n); })};
    if (restates) {
      break;
    }
    if (complete) {
      item ? ++items : ++sentences;
      if ((sentences > MAX_SENTENCES) || (items > MAX_ITEMS)) {
        break;
      }
      seen.insert(key);
      for (QString const &n : std::as_const(numbers)) {
        numbersSeen.insert(n);
      }
    }
    kept = end;
  }
  return text.left(kept).trimmed();
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
  m_fetched.clear();
}

void AssistantEngine::cancel() {
  if (m_busy) {
    m_cancelled = true;
    m_client->cancel();
  }
}

void AssistantEngine::ask(QString const &question, QString const &focusRun, QJsonObject const &view) {
  if (m_busy) {
    emit failed(tr("Still answering the previous question"));
    return;
  }
  if (isControlRequest(question)) {
    // read-only: said at once and the same way every time (the model adds facts nobody asked for)
    QString const answer = tr("I can't do that: I only read the chamber's data. Start, stop and setpoints are on the Control page; "
                              "PID, calibration and limits are in Settings.");
    m_history.push_back(QJsonObject{{"role", "user"}, {"content", question.trimmed()}});
    m_history.push_back(QJsonObject{{"role", "assistant"}, {"content", answer}});
    trimHistory();
    emit answered(answer);
    return;
  }
  QString content = question.trimmed();
  // a day shown on the chart: not about the experiment selected elsewhere in the app
  QString const focus = view.contains(QLatin1String("day")) ? QString{} : focusRun;
  if (!focus.isEmpty()) {
    content = tr("%1\n(Experiment selected in the app: %2. The question may name another experiment.)").arg(content, focus);
  }
  // routing aid for small models: a question naming several runs is answered with one compare_runs call
  QStringList const runs = mentionedRuns(question);
  if (runs.size() >= 2) {
    content += QStringLiteral("\n(Hint: answer with one compare_runs call for runs %1.)").arg(runs.join(QStringLiteral(", ")));
  }
  // asked from the chart: what it shows, so "this view", "here" and chart commands have their experiment and range
  QString const viewRun = view.value("run").toString();
  if (!viewRun.isEmpty()) {
    QStringList const channels = view.value("channels").toVariant().toStringList();
    content += QStringLiteral("\n(Chart shown in the app: experiment %1, visible from %2 s to %3 s after its start (%4 to %5)%6. "
                              "'This view', 'here' and 'visible' mean that range. To change the chart, call plot_run with run %1, "
                              "from_s / to_s in s after the start, and channels (state names) only to change the lines shown.)")
                 .arg(viewRun)
                 .arg(std::round(view.value("from_s").toDouble()))
                 .arg(std::round(view.value("to_s").toDouble()))
                 .arg(view.value("from").toString(), view.value("to").toString(),
                      channels.isEmpty() ? QString{} : QStringLiteral("; lines shown: %1").arg(channels.join(QStringLiteral(", "))));
    // the values in sight, and the lines drawn outside them (the user does not see those)
    if (view.contains(QLatin1String("values_from"))) {
      content += QStringLiteral("\n(Values in sight: %1 to %2.").arg(view.value("values_from").toDouble(), 0, 'g', 4).arg(view.value("values_to").toDouble(), 0, 'g', 4);
      QStringList const offScale = view.value("off_scale").toVariant().toStringList();
      content += offScale.isEmpty() ? QStringLiteral(")")
                                    : QStringLiteral(" Outside them, so not seen on the chart: %1; say so if they matter.)").arg(offScale.join(QStringLiteral(", ")));
    }
  }
  // a day shown (the live data or a recorded day): its experiments and what happened between them
  QString const viewDay = view.value("day").toString();
  if (viewRun.isEmpty() && !viewDay.isEmpty()) {
    content += QStringLiteral("\n(Chart shown in the app: everything recorded on %1, visible from %2 to %3. 'This day', 'today', "
                              "'this view' and 'here' mean it; call get_day_summary with date %1.)")
                 .arg(viewDay, view.value("from").toString(), view.value("to").toString());
  }
  // a comparison shown: its experiments, how they are aligned and the part of the aligned time in sight
  QJsonArray const compared = view.value("compare").toArray();
  if (!compared.isEmpty()) {
    QStringList members;
    QStringList saved;
    for (QJsonValue const &value : compared) {
      QJsonObject const member = value.toObject();
      QString const     ref    = member.value("run").toString();
      QString const     label  = member.value("label").toString();
      members << (ref.isEmpty() ? QStringLiteral("'%1' (not a saved experiment)").arg(label) : QStringLiteral("%1 ('%2')").arg(ref, label));
      if (!ref.isEmpty()) {
        saved << ref;
      }
    }
    QString const align = view.value("align").toString();
    QString const at    = (align == QLatin1String("start")) || align.isEmpty()
                            ? QStringLiteral("their start")
                            : QStringLiteral("%1 %2").arg(QString(align).replace(QLatin1Char('_'), QLatin1Char(' '))).arg(view.value("align_index").toInt(1));
    QStringList const channels = view.value("channels").toVariant().toStringList();
    content += QStringLiteral("\n(Chart shown in the app: a comparison of %1; the first is the reference. The curves are aligned at %2; "
                              "visible from %3 s to %4 s on the aligned time axis (0 s = the alignment point)%5. 'This view', 'this "
                              "comparison', 'these experiments' and 'here' mean it.%6)")
                 .arg(members.join(QStringLiteral(", ")), at)
                 .arg(std::round(view.value("from_s").toDouble()))
                 .arg(std::round(view.value("to_s").toDouble()))
                 .arg(channels.isEmpty() ? QString{} : QStringLiteral("; lines: %1").arg(channels.join(QStringLiteral(", "))),
                      saved.size() >= 2 ? QStringLiteral(" For more, call compare_runs with runs %1 and align %2.").arg(saved.join(QStringLiteral(", ")), align)
                                        : QString{});
  }
  // a quick button: how to answer it
  QString const intent = view.value("intent").toString();
  if (intent == QLatin1String("explain") && !viewRun.isEmpty()) {
    content += QStringLiteral("\n(Answer as a list of three short lines, each under 20 words, and nothing after it: what the experiment "
                              "was set to do (setpoints, PID); how well the temperature followed the setpoint (the main KPIs, the stage "
                              "that went worst); the anomalies detected and how serious they are, or that there are none.)");
  } else if (intent == QLatin1String("similar")) {
    content += QStringLiteral("\n(Answer: better, worse or about the same as the similar experiments, with the KPIs that show it, and "
                              "the best of them.)");
  } else if (intent == QLatin1String("improve")) {
    content += QStringLiteral("\n(Answer: what happened in the step as measured, the PID the AI Advisor suggests (or that the experiment's "
                              "PID is already the best) and what it would change, as predicted. Say it is a suggestion: nothing was changed.)");
  }
  static QRegularExpression const showVerbs(
    QStringLiteral("\\b(?:show|plot|display|open|graph|chart|zoom|go to|jump|focus|fit|hide|add|remove|muestra|mostrar|muéstrame|"
                   "grafica|graficar|abre|abrir|ver)\\b"),
    QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  if (intent.isEmpty() && showVerbs.match(question).hasMatch() && routeQuestion(question).isEmpty()) {
    content += QStringLiteral("\n(Hint: the user wants to see it or change the chart: call plot_run.)");
  }
  m_history.push_back(QJsonObject{{"role", "user"}, {"content", content}});
  trimHistory();
  bool const prefetched{prefetch(question, viewRun.isEmpty() ? focus : viewRun, view)};
  m_round     = 0;
  m_cancelled = false;
  m_corrected = false; // evidence is kept: follow-up questions are checked against everything fetched so far
  m_contextRetried = false;
  m_silent    = false;
  m_question  = question;
  m_firstAnswer.clear();
  // the first round of every question fetches data; the answer comes after it. A quick button's data is all fetched
  // already (improve_run again would repeat the AI Advisor's search)
  m_requireTool = !prefetched;
  m_busy        = true;
  emit busyChanged(true);
  request();
}

void AssistantEngine::trimHistory() {
  // drop the oldest messages, but never start with a tool result or an assistant tool call
  while (m_history.size() > HISTORY_MESSAGES) {
    m_fetched.clear(); // older results may be gone: fetched again when asked about
    m_history.removeFirst();
    while (!m_history.isEmpty() && (m_history.first().toObject().value("role").toString() != QLatin1String("user"))) {
      m_history.removeFirst();
    }
  }
}

bool AssistantEngine::dropOlderHistory(QString const &error) {
  static QRegularExpression const tooLong(QStringLiteral("context|too (?:long|large|many tokens)|exceed"), QRegularExpression::CaseInsensitiveOption);
  if (!tooLong.match(error).hasMatch()) {
    return false;
  }
  // from the question being answered on: its message is the last one of the user that is not a correction
  qsizetype from{-1};
  for (qsizetype i{m_history.size() - 1}; i >= 0; --i) {
    QJsonObject const message = m_history.at(i).toObject();
    if ((message.value("role").toString() == QLatin1String("user")) && !message.value("content").toString().startsWith(QLatin1String("Correction:"))) {
      from = i;
      break;
    }
  }
  if (from <= 0) {
    return false; // nothing older to drop: the question alone does not fit
  }
  QJsonArray kept;
  for (qsizetype i{from}; i < m_history.size(); ++i) {
    kept.push_back(m_history.at(i));
  }
  m_history = kept;
  m_fetched.clear(); // older results are gone: fetched again when asked about
  return true;
}

void AssistantEngine::request() {
  QJsonArray messages{QJsonObject{{"role", "system"}, {"content", systemPrompt()}}};
  for (QJsonValue const &message : std::as_const(m_history)) {
    messages.push_back(message);
  }
  // after the last allowed round the model must answer with what it has
  QJsonArray const tools = (m_round < m_maxRounds) ? m_registry->openAiTools() : QJsonArray{};

  m_streamed.clear();
  m_repetitionStop = false;
  auto onText      = [this](QString const &text) {
    if (m_repetitionStop) {
      return;
    }
    QString const clean = cleanAnswer(text);
    int           pictographs{0};
    withoutMarks(text, &pictographs);
    // cut short (a remark about the answer taken out of it is not a cut: the model goes on), stuck on one token, or
    // decorating its answer with a row of emoji: what came before is the answer
    if ((clean.size() < withoutNoise(text).trimmed().size()) || (pictographs > 2) || loopsAtEnd(text)) {
      // the model started repeating itself: keep what came before and stop it
      m_repetitionStop = true;
      m_streamed       = clean;
      emit partialAnswer(clean);
      m_client->cancel();
      return;
    }
    m_streamed = text;
    emit partialAnswer(clean);
  };

  m_client->chat(
    messages, tools,
    [this](LlmReply const &reply) {
    if (m_repetitionStop) { // stopped on purpose: what came before the loop is the answer
      m_history.push_back(QJsonObject{{"role", "assistant"}, {"content", m_streamed}});
      onAnswer(m_streamed);
      return;
    }
    if (m_cancelled) {
      finish();
      emit failed(tr("Cancelled"));
      return;
    }
    if (!reply.ok) {
      if (!m_contextRetried && dropOlderHistory(reply.error)) {
        m_contextRetried = true;
        request(); // the conversation was longer than the model's context: once more with this question alone
        return;
      }
      finish();
      emit failed(reply.error);
      return;
    }
    m_history.push_back(reply.message);

    if (reply.toolCalls.isEmpty()) {
      onAnswer(reply.content);
      return;
    }

    ++m_round;
    for (LlmToolCall const &call : reply.toolCalls) {
      runTool(call.id, call.name, call.arguments, call.argumentsError);
    }
    request();
    },
    m_silent ? std::function<void(QString const &)>{} : std::function<void(QString const &)>{onText}, 0, std::exchange(m_requireTool, false));
}

bool AssistantEngine::runTool(QString const &id, QString const &name, QJsonObject const &arguments, QString const &argumentsError) {
  emit toolCalled(name, compact(arguments));
  QJsonObject result = argumentsError.isEmpty() ? m_registry->call(name, arguments) : QJsonObject{{"error", argumentsError}};
  bool const  ok{!result.contains("error")};
  // the model reads the card DeepVac wrote (facts with units, table), not the raw data
  QJsonObject const card = result.value("card").toObject();
  QString           text = compact(ok && !card.isEmpty() ? card : result);
  if (text.size() > m_maxResultChars) {
    text = text.left(m_maxResultChars) + QStringLiteral("…(truncated)");
  }
  if (ok && !card.isEmpty()) {
    emit toolCard(name, card);
  }
  emit toolFinished(name, ok, ok ? tr("%1 characters of results").arg(text.size()) : result.value("error").toString());
  m_history.push_back(QJsonObject{{"role", "tool"}, {"tool_call_id", id}, {"name", name}, {"content", text}});
  m_evidence += compact(result) + QLatin1Char('\n');
  // a long conversation: the oldest results go (every answer is checked against all of it)
  if (m_evidence.size() > MAX_EVIDENCE_CHARS) {
    qsizetype const cut{m_evidence.indexOf(QLatin1Char('\n'), m_evidence.size() - MAX_EVIDENCE_CHARS)};
    m_evidence.remove(0, (cut >= 0) ? cut + 1 : m_evidence.size() - MAX_EVIDENCE_CHARS);
  }
  return ok;
}

bool AssistantEngine::prefetch(QString const &question, QString const &focusRun, QJsonObject const &view) {
  // questions over many experiments (rankings, lists) are left to the model's own tool calls
  static QRegularExpression const overMany(
    QStringLiteral("\\b(?:which|best|worst|lowest|highest|rank\\w*|list|all|every|how many|most|least)\\b"),
    QRegularExpression::CaseInsensitiveOption);
  // about the chart's visible range
  static QRegularExpression const aboutView(
    QStringLiteral("\\b(?:this view|the view|in view|here|visible|on screen|on the chart|shown|this range|this part|this section|zoomed)\\b"),
    QRegularExpression::CaseInsensitiveOption);
  QStringList const runs = mentionedRuns(question);
  QVector<std::pair<QString, QJsonObject>> calls;
  // a quick button: what it needs, whatever its words
  QString const intent = view.value("intent").toString();
  QString const viewRun = view.value("run").toString();
  QStringList   compared; // the saved experiments of a comparison shown
  for (QJsonValue const &value : view.value("compare").toArray()) {
    if (QString const ref = value.toObject().value("run").toString(); !ref.isEmpty() && (compared.size() < 5)) {
      compared << ref;
    }
  }
  QString const subject = !viewRun.isEmpty() ? viewRun : compared.value(0); // one experiment: shown, or the comparison's reference
  if ((intent == QLatin1String("explain")) && !viewRun.isEmpty()) {
    // the whole experiment shown: its summary (configuration, KPIs, stages) and its anomalies; a part of it: that part
    double const duration{view.value("duration_s").toDouble()};
    double const from{view.value("from_s").toDouble()};
    double const to{view.value("to_s").toDouble()};
    bool const   whole{(duration <= 0.0) || ((from <= 0.02 * duration) && (to >= 0.98 * duration))};
    if (whole) {
      calls.push_back({QStringLiteral("get_run_summary"), QJsonObject{{"run", viewRun}}});
      calls.push_back({QStringLiteral("get_anomalies"), QJsonObject{{"run", viewRun}}});
    } else {
      QJsonObject range{{"run", viewRun}, {"from_s", std::round(from)}, {"to_s", std::round(to)}};
      calls.push_back({QStringLiteral("get_kpis"), range});
      calls.push_back({QStringLiteral("get_anomalies"), range});
      if (QStringList const channels = view.value("channels").toVariant().toStringList().mid(0, 4); !channels.isEmpty()) {
        range.insert("channels", QJsonArray::fromStringList(channels));
      }
      calls.push_back({QStringLiteral("get_statistics"), range});
    }
  } else if ((intent == QLatin1String("explain")) && (compared.size() >= 2)) {
    // a comparison: how they differ, and the anomalies of each
    QJsonObject arguments{{"runs", QJsonArray::fromStringList(compared)},
                          {"align", view.value("align").toString(QStringLiteral("start"))},
                          {"align_index", view.value("align_index").toInt(1)}};
    if (QString const channel = view.value("channels").toArray().at(0).toString(); !channel.isEmpty()) {
      arguments.insert("channel", channel);
    }
    calls.push_back({QStringLiteral("compare_runs"), arguments});
    for (QString const &ref : compared.mid(0, 3)) {
      calls.push_back({QStringLiteral("get_anomalies"), QJsonObject{{"run", ref}}});
    }
  } else if ((intent == QLatin1String("similar")) && !subject.isEmpty()) {
    calls.push_back({QStringLiteral("find_similar_runs"), QJsonObject{{"run", subject}}});
  } else if ((intent == QLatin1String("improve")) && !subject.isEmpty()) {
    calls.push_back({QStringLiteral("improve_run"), QJsonObject{{"run", subject}}});
  } else if ((intent == QLatin1String("report")) && !subject.isEmpty()) {
    QJsonObject arguments{{"run", subject}};
    if (compared.size() >= 2) {
      arguments.insert("compare_with", QJsonArray::fromStringList(compared.mid(1)));
    }
    calls.push_back({QStringLiteral("generate_report"), arguments});
  }
  QJsonObject const command = (runs.isEmpty() && intent.isEmpty()) ? chartCommand(question, view) : QJsonObject{};
  if (!calls.isEmpty()) {
    // the quick button's data, above
  } else if (!command.isEmpty()) {
    // a chart command: carried out now (the model only confirms it)
    calls.push_back({QStringLiteral("plot_run"), command});
  } else if (QVector<std::pair<QString, QJsonObject>> const routed = (runs.isEmpty() && intent.isEmpty()) ? routeQuestion(question) : QVector<std::pair<QString, QJsonObject>>{};
             !routed.isEmpty()) {
    // about the chamber now, its settings, a day, a list or a ranking: that, not the summary of an experiment
    calls = routed;
  } else if (runs.size() >= 2) {
    calls.push_back({QStringLiteral("compare_runs"), QJsonObject{{"runs", QJsonArray::fromStringList(runs.mid(0, 5))}}});
  } else if (!view.value("compare").toArray().isEmpty() && runs.isEmpty()) {
    // a comparison shown: its saved experiments as aligned on the chart (or their anomalies, or its report)
    QStringList saved;
    for (QJsonValue const &value : view.value("compare").toArray()) {
      if (QString const ref = value.toObject().value("run").toString(); !ref.isEmpty() && (saved.size() < 5)) {
        saved << ref;
      }
    }
    static QRegularExpression const aboutAnomalies(QStringLiteral("\\banomal\\w*"), QRegularExpression::CaseInsensitiveOption);
    static QRegularExpression const aboutReport(QStringLiteral("\\breport\\b"), QRegularExpression::CaseInsensitiveOption);
    if (saved.size() == 1) {
      calls.push_back({QStringLiteral("get_run_summary"), QJsonObject{{"run", saved.first()}}});
    } else if ((saved.size() >= 2) && aboutAnomalies.match(question).hasMatch()) {
      for (QString const &ref : saved) {
        calls.push_back({QStringLiteral("get_anomalies"), QJsonObject{{"run", ref}}});
      }
    } else if ((saved.size() >= 2) && aboutReport.match(question).hasMatch()) {
      calls.push_back({QStringLiteral("generate_report"),
                       QJsonObject{{"run", saved.first()}, {"compare_with", QJsonArray::fromStringList(saved.mid(1))}}});
    } else if (saved.size() >= 2) {
      QJsonObject arguments{{"runs", QJsonArray::fromStringList(saved)},
                            {"align", view.value("align").toString(QStringLiteral("start"))},
                            {"align_index", view.value("align_index").toInt(1)}};
      if (QString const channel = view.value("channels").toArray().at(0).toString(); !channel.isEmpty()) {
        arguments.insert("channel", channel);
      }
      calls.push_back({QStringLiteral("compare_runs"), arguments});
    }
  } else if (view.value("run").toString().isEmpty() && !view.value("day").toString().isEmpty() && runs.isEmpty()) {
    // a day shown: what happened in its visible part
    calls.push_back({QStringLiteral("get_day_summary"), QJsonObject{{"date", view.value("day")},
                                                                    {"from", view.value("from").toString().right(8).left(5)},
                                                                    {"to", view.value("to").toString().right(8).left(5)}}});
  } else if (!view.value("run").toString().isEmpty() && runs.isEmpty() && aboutView.match(question).hasMatch()) {
    // the visible range of the experiment shown: its control KPIs and the statistics of its lines
    QJsonObject range{{"run", view.value("run")}, {"from_s", std::round(view.value("from_s").toDouble())},
                      {"to_s", std::round(view.value("to_s").toDouble())}};
    calls.push_back({QStringLiteral("get_kpis"), range});
    QStringList channels = view.value("channels").toVariant().toStringList().mid(0, 4);
    if (!channels.isEmpty()) {
      range.insert("channels", QJsonArray::fromStringList(channels));
    }
    calls.push_back({QStringLiteral("get_statistics"), range});
  } else if (!runs.isEmpty() || !overMany.match(question).hasMatch()) {
    QString const run = !runs.isEmpty() ? runs.first() : (!focusRun.isEmpty() ? focusRun : QStringLiteral("latest"));
    calls.push_back({QStringLiteral("get_run_summary"), QJsonObject{{"run", run}}});
  }
  // as if the model had asked for them: an assistant tool call message, then the results
  QJsonArray                                toolCalls;
  QVector<std::pair<QString, std::pair<QString, QJsonObject>>> pending;
  QStringList                                                  keys;
  for (auto const &[name, arguments] : calls) {
    QString const key = name + QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact));
    if (m_fetched.contains(key) && (name != QLatin1String("plot_run"))) {
      continue; // fetched already (a chart command is carried out every time)
    }
    QString const id = QStringLiteral("deepvac-%1").arg(++m_prefetchCount);
    toolCalls.push_back(QJsonObject{{"id", id},
                                    {"type", "function"},
                                    {"function", QJsonObject{{"name", name},
                                                             {"arguments", QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact))}}}});
    pending.push_back({id, {name, arguments}});
    keys.push_back(key);
  }
  if (toolCalls.isEmpty()) {
    return false;
  }
  m_history.push_back(QJsonObject{{"role", "assistant"}, {"content", QString{}}, {"tool_calls", toolCalls}});
  for (qsizetype i{0}; i < pending.size(); ++i) {
    // a result with an error (the list still being read, an experiment not found) is fetched again when asked again
    if (runTool(pending[i].first, pending[i].second.first, pending[i].second.second)) {
      m_fetched.insert(keys[i]);
    }
  }
  return true;
}

void AssistantEngine::onAnswer(QString content) {
  content = cleanAnswer(content);
  QString answer = content.isEmpty() ? tr("(no answer)") : content;
  if (!m_groundingCheck) {
    finish();
    emit answered(answer);
    return;
  }
  // values the user gave in the question are known too
  QString const     known   = m_evidence + QLatin1Char('\n') + m_question;
  QStringList       unknown = ungroundedNumbers(content, known);
  bool              kelvin{usesKelvin(content)};
  if ((!unknown.isEmpty() || kelvin) && !m_corrected) {
    // one corrective round, in the background: the shown answer stays unless the new one is better
    m_corrected   = true;
    m_silent      = true;
    m_firstAnswer = content;
    QStringList problems;
    if (!unknown.isEmpty()) {
      problems << QStringLiteral("these values are in no tool result: %1").arg(unknown.join(QStringLiteral(", ")));
    }
    if (kelvin) {
      problems << QStringLiteral("temperatures must be in °C, never Kelvin");
    }
    m_history.push_back(QJsonObject{
      {"role", "user"},
      {"content", QStringLiteral("Correction: %1. Write the answer to my previous question again, once, in at most 4 short "
                                 "sentences, with values exactly as written in the results (°C). If you need data you do not "
                                 "have, call a tool.")
                    .arg(problems.join(QStringLiteral("; ")))}});
    emit checkingAnswer();
    // an answer with invented numbers means the model skipped the data: this time it must call a tool
    m_requireTool = (m_round < m_maxRounds);
    request();
    return;
  }
  if (m_corrected && !m_firstAnswer.isEmpty()) {
    // keep the corrected answer only if it is better than the one shown
    auto score = [&known](QString const &text) {
      return static_cast<int>(ungroundedNumbers(text, known).size()) + (usesKelvin(text) ? 5 : 0) + (text.trimmed().isEmpty() ? 50 : 0);
    };
    if (score(content) >= score(m_firstAnswer)) {
      answer  = m_firstAnswer;
      unknown = ungroundedNumbers(m_firstAnswer, known);
      kelvin  = usesKelvin(m_firstAnswer);
    }
  }
  if (!unknown.isEmpty()) {
    answer += QStringLiteral("\n\n")
              + tr("⚠ Not found in the analysis results: %1. Check these values on the Analysis page.").arg(unknown.join(QStringLiteral(", ")));
  }
  if (kelvin) {
    answer += QStringLiteral("\n\n") + tr("⚠ DeepVac reports temperatures in °C; values in Kelvin did not come from the analysis.");
  }
  finish();
  emit answered(answer);
}

void AssistantEngine::finish() {
  m_busy = false;
  emit busyChanged(false);
}

}
