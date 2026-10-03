#ifndef ASSISTANTENGINE_H
#define ASSISTANTENGINE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

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

  void ask(QString const &question, QString const &focusRun = {});
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
  // temperatures in Kelvin ("298.15 K", "kelvin"): never produced by DeepVac
  static bool usesKelvin(QString const &answer);
  void setMaxResultChars(int chars);
  [[nodiscard]] QString systemPrompt() const;

signals:
  void toolCalled(QString const &name, QString const &arguments);
  void toolFinished(QString const &name, bool ok, QString const &summary);
  void toolCard(QString const &name, QJsonObject const &card); // DeepVac's rendering of a tool result
  void partialAnswer(QString const &text);                     // the answer so far, while it is generated
  void answerRetracted();                                      // the streamed answer failed the checks and is being redone
  void answered(QString const &text);
  void failed(QString const &error);
  void busyChanged(bool busy);

private:
  void request();
  void finish();
  void trimHistory();

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
};

} // namespace deepvaccontrol::assistant

#endif // ASSISTANTENGINE_H
