#include "toolregistry.h"

#include <exception>

namespace deepvaccontrol::assistant {

namespace {
bool matchesType(QJsonValue const &value, QString const &type) {
  if (type == QLatin1String("string")) {
    return value.isString();
  }
  if (type == QLatin1String("number")) {
    return value.isDouble();
  }
  if (type == QLatin1String("integer")) {
    return value.isDouble() && (value.toDouble() == static_cast<double>(static_cast<qint64>(value.toDouble())));
  }
  if (type == QLatin1String("boolean")) {
    return value.isBool();
  }
  if (type == QLatin1String("array")) {
    return value.isArray();
  }
  if (type == QLatin1String("object")) {
    return value.isObject();
  }
  return true;
}
} // namespace

void ToolRegistry::add(ToolSpec spec) {
  m_tools.push_back(std::move(spec));
}

bool ToolRegistry::contains(QString const &name) const {
  return std::any_of(m_tools.cbegin(), m_tools.cend(), [&](ToolSpec const &tool) { return tool.name == name; });
}

QStringList ToolRegistry::names() const {
  QStringList names;
  for (ToolSpec const &tool : m_tools) {
    names << tool.name;
  }
  return names;
}

QJsonArray ToolRegistry::openAiTools() const {
  QJsonArray tools;
  for (ToolSpec const &tool : m_tools) {
    tools.push_back(QJsonObject{
      {"type", "function"},
      {"function", QJsonObject{{"name", tool.name}, {"description", tool.description}, {"parameters", tool.parameters}}}});
  }
  return tools;
}

QString ToolRegistry::validate(QJsonObject const &schema, QJsonObject const &arguments) {
  QJsonObject const properties = schema.value(QLatin1String("properties")).toObject();
  for (QJsonValue const &required : schema.value(QLatin1String("required")).toArray()) {
    if (!arguments.contains(required.toString())) {
      return QStringLiteral("missing required argument '%1'").arg(required.toString());
    }
  }
  for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
    if (!properties.contains(it.key())) {
      return QStringLiteral("unknown argument '%1' (allowed: %2)").arg(it.key(), properties.keys().join(QStringLiteral(", ")));
    }
    QJsonValue const type = properties.value(it.key()).toObject().value(QLatin1String("type"));
    // "type" may be a list ("string" or "integer")
    bool             typeOk{type.isUndefined()};
    for (QJsonValue const &candidate : type.isArray() ? type.toArray() : QJsonArray{type}) {
      typeOk = typeOk || matchesType(it.value(), candidate.toString());
    }
    if (!typeOk && !it.value().isNull()) {
      return QStringLiteral("argument '%1' must be of type %2").arg(it.key(), type.isArray() ? QStringLiteral("string/number") : type.toString());
    }
  }
  return {};
}

QJsonObject ToolRegistry::call(QString const &name, QJsonObject const &arguments) const {
  auto const tool = std::find_if(m_tools.cbegin(), m_tools.cend(), [&](ToolSpec const &t) { return t.name == name; });
  if (tool == m_tools.cend()) {
    return QJsonObject{{"error", QStringLiteral("unknown tool '%1'. Available tools: %2").arg(name, names().join(QStringLiteral(", ")))}};
  }
  if (QString const invalid = validate(tool->parameters, arguments); !invalid.isEmpty()) {
    return QJsonObject{{"error", invalid}};
  }
  try {
    return tool->handler(arguments);
  } catch (std::exception const &exception) {
    return QJsonObject{{"error", QStringLiteral("%1 failed: %2").arg(name, QString::fromUtf8(exception.what()))}};
  } catch (...) {
    return QJsonObject{{"error", QStringLiteral("%1 failed").arg(name)}};
  }
}

} // namespace deepvaccontrol::assistant
