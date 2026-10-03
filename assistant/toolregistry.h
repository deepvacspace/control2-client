#ifndef TOOLREGISTRY_H
#define TOOLREGISTRY_H

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <functional>

namespace deepvaccontrol::assistant {

// One function the model may call. Handlers return a JSON object; {"error": "..."} on failure.
struct ToolSpec {
  QString                                           name;
  QString                                           description;
  QJsonObject                                       parameters; // JSON schema of the arguments (type: object)
  std::function<QJsonObject(QJsonObject const &)>   handler;
};

/**
 * @brief The functions exposed to the language model. The model only chooses a tool and its
 * arguments; DeepVac validates them and does the work. Unknown tools, invalid arguments and
 * handler exceptions come back to the model as errors instead of failing the conversation.
 */
class ToolRegistry {
public:
  void                         add(ToolSpec spec);
  [[nodiscard]] bool           contains(QString const &name) const;
  [[nodiscard]] QStringList    names() const;
  [[nodiscard]] QJsonArray     openAiTools() const; // "tools" of /v1/chat/completions
  [[nodiscard]] QJsonObject    call(QString const &name, QJsonObject const &arguments) const;

  // the arguments object matches the schema's required fields and basic types; empty when fine
  static QString validate(QJsonObject const &schema, QJsonObject const &arguments);

private:
  QVector<ToolSpec> m_tools;
};

} // namespace deepvaccontrol::assistant

#endif // TOOLREGISTRY_H
