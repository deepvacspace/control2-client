#include "toolregistry.h"

#include <algorithm>
#include <cmath>
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

// The arguments as meant: a small model sends "3" for 3, 3 for "3", one value where a list is asked for, and null or
// "" for what it leaves out. Read so instead of failing the call (what cannot be read stays, for validate() to name)
QJsonObject normalized(QJsonObject const &schema, QJsonObject const &arguments) {
  QJsonObject const properties = schema.value(QLatin1String("properties")).toObject();
  QStringList       required;
  for (QJsonValue const &name : schema.value(QLatin1String("required")).toArray()) {
    required << name.toString();
  }
  QJsonObject result;
  for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
    QJsonValue value = it.value();
    // left out
    if (value.isNull() || (value.isString() && value.toString().trimmed().isEmpty() && !required.contains(it.key()))) {
      continue;
    }
    QJsonValue const type = properties.value(it.key()).toObject().value(QLatin1String("type"));
    QStringList      types;
    for (QJsonValue const &candidate : type.isArray() ? type.toArray() : QJsonArray{type}) {
      types << candidate.toString();
    }
    bool const fits{type.isUndefined()
                    || std::any_of(types.cbegin(), types.cend(), [&value](QString const &t) { return matchesType(value, t); })};
    if (!fits) {
      bool const wantsNumber{types.contains(QLatin1String("number")) || types.contains(QLatin1String("integer"))};
      if (value.isString() && wantsNumber) {
        // "20", "0,5", "−40 °C"
        QString text = value.toString();
        text.remove(QStringLiteral("°C")).remove(QChar(0x00B0)).replace(QChar(0x2212), QLatin1Char('-')).replace(QLatin1Char(','), QLatin1Char('.'));
        bool         ok{false};
        double const number{text.trimmed().toDouble(&ok)};
        if (ok && (types.contains(QLatin1String("number")) || (number == std::floor(number)))) {
          value = number;
        }
      } else if (value.isDouble() && types.contains(QLatin1String("string"))) {
        double const number{value.toDouble()};
        value = (number == std::floor(number)) && (std::fabs(number) < 1e15) ? QString::number(static_cast<qint64>(number))
                                                                              : QString::number(number, 'g', 12);
      } else if (value.isBool() && types.contains(QLatin1String("string"))) {
        value = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
      } else if (value.isString() && types.contains(QLatin1String("boolean"))) {
        QString const text = value.toString().trimmed().toLower();
        if ((text == QLatin1String("true")) || (text == QLatin1String("false"))) {
          value = (text == QLatin1String("true"));
        }
      } else if ((value.isString() || value.isDouble()) && types.contains(QLatin1String("array"))) {
        value = QJsonArray{value};
      }
    }
    result.insert(it.key(), value);
  }
  return result;
}
}

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
  QJsonObject const given = normalized(tool->parameters, arguments);
  if (QString const invalid = validate(tool->parameters, given); !invalid.isEmpty()) {
    return QJsonObject{{"error", invalid}};
  }
  try {
    return tool->handler(given);
  } catch (std::exception const &exception) {
    return QJsonObject{{"error", QStringLiteral("%1 failed: %2").arg(name, QString::fromUtf8(exception.what()))}};
  } catch (...) {
    return QJsonObject{{"error", QStringLiteral("%1 failed").arg(name)}};
  }
}

}
