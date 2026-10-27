#include "llmclient.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QMap>
#include <memory>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace deepvaccontrol::assistant {

namespace {
QString stripThinking(QString text) {
  static QRegularExpression const think(QStringLiteral("<think>.*?</think>"), QRegularExpression::DotMatchesEverythingOption);
  text.remove(think);
  // an unterminated block (generation cut short) is dropped entirely
  if (int const open = static_cast<int>(text.indexOf(QStringLiteral("<think>"))); open >= 0) {
    text.truncate(open);
  }
  return text.trimmed();
}

QJsonObject parseArguments(QJsonValue const &value, QString &error) {
  if (value.isObject()) {
    return value.toObject();
  }
  QString const text = value.toString().trimmed();
  if (text.isEmpty()) {
    return {};
  }
  QJsonParseError    parseError{};
  QJsonDocument const doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    error = QStringLiteral("arguments are not a JSON object: %1").arg(text.left(200));
    return {};
  }
  return doc.object();
}
}

LlmClient::LlmClient(QObject *parent) : QObject{parent}, m_network{new QNetworkAccessManager(this)} {
  // never go through a system proxy: the server is on this machine
  m_network->setProxy(QNetworkProxy::NoProxy);
}

bool LlmClient::isLoopback(QUrl const &url) {
  if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
    return false;
  }
  QString const host = url.host();
  if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  QHostAddress const address(host);
  return !address.isNull() && address.isLoopback();
}

bool LlmClient::setBaseUrl(QUrl const &url) {
  if (!isLoopback(url)) {
    return false;
  }
  m_baseUrl = url;
  return true;
}

QUrl LlmClient::baseUrl() const {
  return m_baseUrl;
}

void LlmClient::setOptions(LlmOptions const &options) {
  m_options = options;
}

void LlmClient::setTimeoutMs(int timeoutMs) {
  m_timeoutMs = timeoutMs;
}

bool LlmClient::busy() const {
  return !m_pending.isNull();
}

void LlmClient::cancel() {
  if (m_pending) {
    m_pending->abort();
  }
}

LlmReply LlmClient::parseCompletion(QJsonObject const &response) {
  LlmReply reply;
  if (response.contains(QLatin1String("error"))) {
    QJsonValue const error = response.value(QLatin1String("error"));
    reply.error            = error.isObject() ? error.toObject().value(QLatin1String("message")).toString() : error.toString();
    return reply;
  }
  QJsonObject const message =
    response.value(QLatin1String("choices")).toArray().first().toObject().value(QLatin1String("message")).toObject();
  if (message.isEmpty()) {
    reply.error = QStringLiteral("the server returned no message");
    return reply;
  }

  QString content = message.value(QLatin1String("content")).toString();
  // some templates put the reasoning in a separate field: it is never shown nor kept
  content = stripThinking(content);

  int generated{0};
  for (QJsonValue const &value : message.value(QLatin1String("tool_calls")).toArray()) {
    QJsonObject const call     = value.toObject();
    QJsonObject const function = call.value(QLatin1String("function")).toObject();
    LlmToolCall       toolCall;
    toolCall.id        = call.value(QLatin1String("id")).toString();
    toolCall.name      = function.value(QLatin1String("name")).toString();
    toolCall.arguments = parseArguments(function.value(QLatin1String("arguments")), toolCall.argumentsError);
    if (toolCall.id.isEmpty()) {
      toolCall.id = QStringLiteral("call_%1").arg(++generated);
    }
    reply.toolCalls.push_back(toolCall);
  }

  // fallback: <tool_call>{"name": ..., "arguments": {...}}</tool_call> left in the text
  if (reply.toolCalls.isEmpty()) {
    static QRegularExpression const inlineCall(QStringLiteral("<tool_call>\\s*(\\{.*?\\})\\s*</tool_call>"),
                                               QRegularExpression::DotMatchesEverythingOption);
    for (auto it = inlineCall.globalMatch(content); it.hasNext();) {
      QJsonObject const call = QJsonDocument::fromJson(it.next().captured(1).toUtf8()).object();
      if (call.value(QLatin1String("name")).toString().isEmpty()) {
        continue;
      }
      LlmToolCall toolCall;
      toolCall.id        = QStringLiteral("call_%1").arg(++generated);
      toolCall.name      = call.value(QLatin1String("name")).toString();
      toolCall.arguments = parseArguments(call.value(QLatin1String("arguments")), toolCall.argumentsError);
      reply.toolCalls.push_back(toolCall);
    }
    if (!reply.toolCalls.isEmpty()) {
      content.remove(inlineCall);
      content = content.trimmed();
    }
  }

  reply.content = content;
  reply.ok      = true;

  // the message as it goes back into the conversation (standard tool_calls form)
  QJsonObject assistantMessage{{"role", "assistant"}, {"content", content}};
  if (!reply.toolCalls.isEmpty()) {
    QJsonArray calls;
    for (LlmToolCall const &call : std::as_const(reply.toolCalls)) {
      calls.push_back(QJsonObject{
        {"id", call.id},
        {"type", "function"},
        {"function", QJsonObject{{"name", call.name},
                                 {"arguments", QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact))}}}});
    }
    assistantMessage.insert("tool_calls", calls);
  }
  reply.message = assistantMessage;
  return reply;
}

void LlmClient::chat(QJsonArray const &messages, QJsonArray const &tools, std::function<void(LlmReply const &)> done,
                     std::function<void(QString const &)> onText, int maxTokens, bool requireTool) {
  if (m_pending) {
    LlmReply reply;
    reply.error = QStringLiteral("a request is already running");
    done(reply);
    return;
  }
  bool const  streaming{static_cast<bool>(onText)};
  QJsonObject body{{"messages", messages},
                   {"temperature", m_options.temperature},
                   {"max_tokens", maxTokens > 0 ? maxTokens : m_options.maxTokens},
                   {"stream", streaming},
                   {"chat_template_kwargs", QJsonObject{{"enable_thinking", m_options.enableThinking}}}};
  if (!tools.isEmpty()) {
    body.insert("tools", tools);
    body.insert("tool_choice", requireTool ? "required" : "auto"); // required: the model must fetch data before answering
  }

  QNetworkRequest request(m_baseUrl.resolved(QUrl(QStringLiteral("/v1/chat/completions"))));
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setTransferTimeout(m_timeoutMs); // inactivity: a streamed answer keeps it alive
  QNetworkReply *networkReply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  m_pending                   = networkReply;

  // server-sent events: the message is assembled from the deltas, text is shown as it arrives
  struct Stream {
    QByteArray            buffer;
    QByteArray            raw; // whole body, for a non-streamed (error) answer
    QString               content;
    QMap<int, QJsonObject> calls; // index -> {id, name, arguments}
    bool                  sawEvents{false};
  };
  auto stream = std::make_shared<Stream>();
  if (streaming) {
    connect(networkReply, &QNetworkReply::readyRead, this, [networkReply, stream, onText] {
      QByteArray const chunk = networkReply->readAll();
      stream->raw += chunk;
      stream->buffer += chunk;
      bool textChanged{false};
      for (qsizetype end = stream->buffer.indexOf('\n'); end >= 0; end = stream->buffer.indexOf('\n')) {
        QByteArray const line = stream->buffer.left(end).trimmed();
        stream->buffer.remove(0, end + 1);
        if (!line.startsWith("data:")) {
          continue;
        }
        stream->sawEvents       = true;
        QByteArray const payload = line.mid(5).trimmed();
        if (payload == "[DONE]") {
          continue;
        }
        QJsonObject const delta =
          QJsonDocument::fromJson(payload).object().value("choices").toArray().first().toObject().value("delta").toObject();
        if (delta.contains("content") && !delta.value("content").isNull()) {
          stream->content += delta.value("content").toString();
          textChanged = true;
        }
        for (QJsonValue const &value : delta.value("tool_calls").toArray()) {
          QJsonObject const part     = value.toObject();
          int const         index    = part.value("index").toInt();
          QJsonObject       call     = stream->calls.value(index);
          QJsonObject const function = part.value("function").toObject();
          if (part.contains("id") && !part.value("id").toString().isEmpty()) {
            call["id"] = part.value("id");
          }
          if (function.contains("name") && !function.value("name").toString().isEmpty()) {
            call["name"] = call.value("name").toString() + function.value("name").toString();
          }
          call["arguments"] = call.value("arguments").toString() + function.value("arguments").toString();
          stream->calls.insert(index, call);
        }
      }
      if (textChanged) {
        // what the user may see: no reasoning, nothing from an inline tool call on
        QString visible = stripThinking(stream->content);
        if (int const call = static_cast<int>(visible.indexOf(QStringLiteral("<tool_call>"))); call >= 0) {
          visible.truncate(call);
        }
        if (!visible.trimmed().isEmpty()) {
          onText(visible.trimmed());
        }
      }
    });
  }
  connect(networkReply, &QNetworkReply::finished, this, [this, networkReply, done, stream, streaming] {
    networkReply->deleteLater();
    m_pending = nullptr; // done() may start the next request right away
    QByteArray const rest = networkReply->readAll();
    stream->raw += rest;
    LlmReply reply;
    if (streaming && stream->sawEvents) {
      QJsonObject message{{"role", "assistant"}, {"content", stream->content}};
      if (!stream->calls.isEmpty()) {
        QJsonArray calls;
        for (QJsonObject const &call : std::as_const(stream->calls)) {
          calls.push_back(QJsonObject{{"id", call.value("id")},
                                      {"type", "function"},
                                      {"function", QJsonObject{{"name", call.value("name")}, {"arguments", call.value("arguments")}}}});
        }
        message.insert("tool_calls", calls);
      }
      reply = parseCompletion(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", message}}}}});
    } else {
      QJsonDocument const doc = QJsonDocument::fromJson(stream->raw);
      if (doc.isObject()) {
        reply = parseCompletion(doc.object());
      }
    }
    if (networkReply->error() != QNetworkReply::NoError && reply.error.isEmpty() && !reply.ok) {
      reply.error = (networkReply->error() == QNetworkReply::OperationCanceledError) ? QStringLiteral("cancelled")
                                                                                       : networkReply->errorString();
    } else if (!reply.ok && reply.error.isEmpty()) {
      reply.error = QStringLiteral("unreadable answer from the model server");
    }
    done(reply);
  });
}

void LlmClient::checkHealth(std::function<void(bool, QString const &)> done) {
  QNetworkRequest request(m_baseUrl.resolved(QUrl(QStringLiteral("/health"))));
  request.setTransferTimeout(3000);
  QNetworkReply *reply = m_network->get(request);
  connect(reply, &QNetworkReply::finished, this, [reply, done] {
    reply->deleteLater();
    int const        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QJsonObject const body  = QJsonDocument::fromJson(reply->readAll()).object();
    QString const     state = body.value(QLatin1String("status")).toString();
    if ((status == 200) && (state.isEmpty() || state == QLatin1String("ok"))) {
      done(true, QStringLiteral("ok"));
    } else if (status == 503) {
      done(false, QStringLiteral("loading model"));
    } else {
      done(false, reply->error() != QNetworkReply::NoError ? reply->errorString() : QStringLiteral("HTTP %1").arg(status));
    }
  });
}

}
