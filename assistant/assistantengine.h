#ifndef ASSISTANTENGINE_H
#define ASSISTANTENGINE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QVector>
#include <utility>

namespace deepvaccontrol::assistant {

class LlmClient;
class ToolRegistry;

/**
 * @brief Conversation with the local model: the model reads the question, picks tools from the
 * registry, DeepVac runs them and hands the structured results back, until the model answers.
 *
 * The model never computes: the system prompt tells it to take every number from a tool result.
 * Tool rounds per question and the size of each result are bounded (small context of a 1-2 B model).
 */
class AssistantEngine : public QObject {
  Q_OBJECT
public:
  AssistantEngine(LlmClient *client, ToolRegistry const *registry, QObject *parent = nullptr);

  // view: the chart the question was asked from ({run "#N", from_s, to_s (s after the run start), from, to (clock),
  // channels [names]}; a day: {day, from, to}; a comparison: {compare [{run, label}], align, align_index, from_s, to_s
  // (s from the alignment point), channels}); "this view" questions get the data of what is shown fetched first
  void ask(QString const &question, QString const &focusRun = {}, QJsonObject const &view = {});
  // lets the server process the fixed part of every request (system prompt, tools) ahead of the first question
  void warmUp();
  void cancel();
  void reset();
  [[nodiscard]] bool busy() const;

  void setMaxToolRounds(int rounds);
  // numbers in an answer that appear in none of the tool results are pointed out to the user
  void setGroundingCheck(bool enabled);
  // the numbers of `answer` not found in `evidence` (tool results), allowing rounding,
  // sign and s -> min / h conversions; run / stage numbers, dates and small counts are ignored
  static QStringList ungroundedNumbers(QString const &answer, QString const &evidence);
  // runs a question names ("run 105 and 108", "#3 vs #4") as "#N"
  static QStringList mentionedRuns(QString const &question);
  // a chart command about the view shown ("zoom to the second hold", "show the last 2 h", "fit", "zoom in"):
  // plot_run's arguments, or empty when the question is no such command
  static QJsonObject chartCommand(QString const &question, QJsonObject const &view);
  // an order to operate the chamber or change its settings ("start the chamber at 80 °C"): answered at once, the
  // assistant only reads
  static bool isControlRequest(QString const &question);
  // what a question that names no experiment is about, as the tool calls that answer it: the chamber now
  // (get_live_status), its settings (get_settings), a day (get_day_summary), the best or worst experiment (rank_runs),
  // a list of experiments (list_runs), a prediction or a PID for a setpoint (predict_setpoint, suggest_pid); empty when
  // it is none of these (then the experiment in question is looked up)
  static QVector<std::pair<QString, QJsonObject>> routeQuestion(QString const &question);
  // temperatures in Kelvin ("298.15 K", "kelvin"): never produced by DeepVac
  static bool usesKelvin(QString const &answer);
  // the answer up to the first sentence it repeats (small models can loop), without check-mark / pin emojis
  static QString cleanAnswer(QString const &answer);
  // the answer without the check marks and the remarks about itself in parentheses ("(final answer)") small models add
  static QString withoutNoise(QString const &answer);
  void setMaxResultChars(int chars);
  [[nodiscard]] QString systemPrompt() const;

signals:
  void toolCalled(QString const &name, QString const &arguments);
  void toolFinished(QString const &name, bool ok, QString const &summary);
  void toolCard(QString const &name, QJsonObject const &card); // DeepVac's rendering of a tool result
  void partialAnswer(QString const &text);                     // the answer so far, while it is generated
  // the answer shown has values that are in no result: a second answer is asked for in the background, and replaces
  // the shown one only if it is better
  void checkingAnswer();
  void answered(QString const &text);
  void failed(QString const &error);
  void busyChanged(bool busy);

private:
  void request();
  // the reply without tool calls: checked, maybe corrected once, then answered()
  void onAnswer(QString content);
  void finish();
  void trimHistory();
  // runs a tool and adds its result to the conversation (the model reads DeepVac's card of it)
  // true when the tool gave a result (not an error)
  bool runTool(QString const &id, QString const &name, QJsonObject const &arguments, QString const &argumentsError = {});
  // the model server refused the request as longer than its context: everything before the question being answered
  // is dropped; false when the error is another one or nothing older is left
  bool dropOlderHistory(QString const &error);
  // the data a question is about, fetched by DeepVac before the model answers: the model cannot skip it
  // (llama-server does not enforce tool_choice "required"); experiments already fetched are not fetched again
  // the data a question needs, fetched before the model is asked; true when something was fetched
  bool prefetch(QString const &question, QString const &focusRun, QJsonObject const &view);

  LlmClient          *m_client;
  ToolRegistry const *m_registry;
  QJsonArray          m_history; // without the system prompt
  int                 m_round{0};
  int                 m_maxRounds{6};
  int                 m_maxResultChars{6000};
  bool                m_busy{false};
  bool                m_cancelled{false};
  bool                m_groundingCheck{true};
  bool                m_corrected{false}; // the corrective round of this question was used
  bool                m_requireTool{false}; // the next request must call a tool (corrective round)
  QString             m_evidence; // tool results of the current question
  QSet<QString>       m_fetched;  // what prefetch() put in the conversation still kept
  int                 m_prefetchCount{0};
  QString             m_question; // its numbers count as known (e.g. "did it reach 100 °C?")
  bool                m_silent{false}; // the corrective round: nothing is streamed
  QString             m_firstAnswer;   // the answer shown before the corrective round
  QString             m_streamed;      // streamed so far (cut when the model starts repeating itself)
  bool                m_repetitionStop{false};
  bool                m_contextRetried{false}; // this question was already asked again without the older conversation
};

}

#endif // ASSISTANTENGINE_H
