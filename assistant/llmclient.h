#ifndef LLMCLIENT_H
#define LLMCLIENT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace deepvaccontrol::assistant {

struct LlmToolCall {
  QString     id;
  QString     name;
  QJsonObject arguments;
  QString     argumentsError; // set when the model produced arguments that are not a JSON object
};

struct LlmReply {
  bool                 ok{false};
  QString              error;
  QString              content; // without <think> blocks / inline tool calls
  QVector<LlmToolCall> toolCalls;
  QJsonObject          message; // assistant message to append to the conversation
};

struct LlmOptions {
  double temperature{0.3};
  int    maxTokens{400}; // tool calls are short and answers are a few sentences: long generations cost time
  bool   enableThinking{false}; // Qwen3 chat template switch (ignored by other templates)
};

/**
 * @brief Chat client for a local OpenAI-compatible server (llama.cpp's llama-server, or any other
 * runtime with the same /v1/chat/completions API), so the model can be swapped freely.
 *
 * Only loopback addresses are accepted: the assistant must stay fully offline.
 * Tool calls are read from the standard `tool_calls` field and, as a fallback for templates that
 * leave them in the text, from Hermes / Qwen style `<tool_call>{...}</tool_call>` blocks.
 */
class LlmClient : public QObject {
  Q_OBJECT
public:
  explicit LlmClient(QObject *parent = nullptr);

  // false (and unchanged) for a non-loopback URL
  bool               setBaseUrl(QUrl const &url);
  [[nodiscard]] QUrl baseUrl() const;
  void               setOptions(LlmOptions const &options);
  void               setTimeoutMs(int timeoutMs);

  // onText set: the answer is streamed and onText gets the visible text so far (no reasoning, no
  // tool-call markup); maxTokens > 0 overrides LlmOptions::maxTokens for this request
  void chat(QJsonArray const &messages, QJsonArray const &tools, std::function<void(LlmReply const &)> done,
            std::function<void(QString const &)> onText = {}, int maxTokens = 0, bool requireTool = false);
  void cancel();
  [[nodiscard]] bool busy() const;

  // GET /health (llama-server) -- ok when the model is loaded and the server answers
  void checkHealth(std::function<void(bool ok, QString const &detail)> done);

  static bool    isLoopback(QUrl const &url);
  // parsing, exposed for tests
  static LlmReply parseCompletion(QJsonObject const &response);

private:
  QNetworkAccessManager *m_network;
  QUrl                   m_baseUrl{QStringLiteral("http://127.0.0.1:8089")};
  LlmOptions             m_options;
  int                    m_timeoutMs{180'000};
  QPointer<QNetworkReply> m_pending;
};

}

#endif // LLMCLIENT_H
