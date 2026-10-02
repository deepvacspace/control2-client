#include "derivedvariablesstore.h"

#include <QSqlError>
#include <QSqlQuery>

#include "derivedexpression.h"
#include "sqlitedb.h"

namespace deepvaccontrol::storage {

namespace {
QString const DERIVED_VARIABLES_DB{"deepvac_derived_variables.sqlite3"};

bool ensureSchema(ScopedDb &db) {
  if (!db.isOpen()) {
    return false;
  }
  db.exec(R"(CREATE TABLE IF NOT EXISTS derived_variables (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            type TEXT NOT NULL,
            source_channel TEXT,
            source_channel2 TEXT,
            window INTEGER,
            expression TEXT,
            color TEXT NOT NULL,
            created_by TEXT NOT NULL DEFAULT 'Unknown',
            created_at TEXT NOT NULL
        ))");
  return true;
}

QVariant nullable(QString const &value) {
  return value.isEmpty() ? QVariant() : QVariant(value);
}

DerivedVariable readRow(QSqlQuery const &q) {
  DerivedVariable definition;
  definition.id             = q.value("id").toLongLong();
  definition.name           = q.value("name").toString();
  definition.type           = DerivedVariable::typeFromKey(q.value("type").toString()).value_or(DerivedVariable::Type::Custom);
  definition.sourceChannel  = q.value("source_channel").toString();
  definition.sourceChannel2 = q.value("source_channel2").toString();
  definition.window         = q.value("window").toInt();
  definition.expression     = q.value("expression").toString();
  definition.color          = QColor(q.value("color").toString());
  definition.createdBy      = q.value("created_by").toString();
  definition.createdAt      = q.value("created_at").toString();
  return definition;
}

QVector<QPair<DerivedVariable::Type, QString>> const &typeKeys() {
  static QVector<QPair<DerivedVariable::Type, QString>> const keys{
    {DerivedVariable::Type::Difference, "difference"},
    {DerivedVariable::Type::RateOfChange, "rate_of_change"},
    {DerivedVariable::Type::RollingStd, "rolling_std"},
    {DerivedVariable::Type::CumulativeIntegral, "cumulative_integral"},
    {DerivedVariable::Type::Custom, "custom"}};
  return keys;
}
} // namespace

QString DerivedVariable::typeKey(Type type) {
  for (auto const &[candidate, key] : typeKeys()) {
    if (candidate == type) {
      return key;
    }
  }
  return {};
}

std::optional<DerivedVariable::Type> DerivedVariable::typeFromKey(QString const &key) {
  for (auto const &[type, candidate] : typeKeys()) {
    if (candidate == key) {
      return type;
    }
  }
  return std::nullopt;
}

QVector<DerivedVariable> DerivedVariablesStore::list(QString *error) {
  ScopedDb db(DERIVED_VARIABLES_DB);
  if (!ensureSchema(db)) {
    if (error) {
      *error = db.lastError();
    }
    return {};
  }
  QVector<DerivedVariable> result;
  QSqlQuery                q = db.query("SELECT * FROM derived_variables ORDER BY name");
  while (q.next()) {
    result.push_back(readRow(q));
  }
  return result;
}

bool DerivedVariablesStore::validate(DerivedVariable const &definition, QString *error) {
  auto fail = [error](QString const &text) {
    if (error) {
      *error = text;
    }
    return false;
  };
  using Type = DerivedVariable::Type;
  if (definition.name.trimmed().isEmpty()) {
    return fail(QObject::tr("Name is required."));
  }
  if ((definition.type == Type::Difference) && (definition.sourceChannel.isEmpty() || definition.sourceChannel2.isEmpty())) {
    return fail(QObject::tr("'Difference' needs two source channels."));
  }
  if (((definition.type == Type::RateOfChange) || (definition.type == Type::RollingStd)
       || (definition.type == Type::CumulativeIntegral))
      && definition.sourceChannel.isEmpty()) {
    return fail(QObject::tr("This type needs a source channel."));
  }
  if ((definition.type == Type::RollingStd) && (definition.window < 2)) {
    return fail(QObject::tr("'Rolling standard deviation' needs a window of at least 2 samples."));
  }
  if (definition.type == Type::Custom) {
    if (definition.expression.trimmed().isEmpty()) {
      return fail(QObject::tr("A custom derived variable needs an expression."));
    }
    QString parseError;
    if (!DerivedExpression::parse(definition.expression, &parseError)) {
      return fail(parseError);
    }
  }
  return true;
}

std::optional<DerivedVariable> DerivedVariablesStore::add(DerivedVariable const &definition, QString *error) {
  if (!validate(definition, error)) {
    return std::nullopt;
  }
  ScopedDb db(DERIVED_VARIABLES_DB);
  ensureSchema(db);
  using Type        = DerivedVariable::Type;
  bool const usesA  = definition.type != Type::Custom;
  bool const usesB  = definition.type == Type::Difference;
  QSqlQuery  q      = db.query("INSERT INTO derived_variables (name, type, source_channel, source_channel2, window, "
                               "expression, color, created_by, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                               {definition.name.trimmed(),
                                DerivedVariable::typeKey(definition.type),
                                usesA ? nullable(definition.sourceChannel) : QVariant(),
                                usesB ? nullable(definition.sourceChannel2) : QVariant(),
                                definition.type == Type::RollingStd ? QVariant(definition.window) : QVariant(),
                                definition.type == Type::Custom ? nullable(definition.expression.trimmed()) : QVariant(),
                                definition.color.isValid() ? definition.color.name() : QStringLiteral("#2563eb"),
                                definition.createdBy.isEmpty() ? QStringLiteral("Unknown") : definition.createdBy,
                                nowIso()});
  if (q.lastError().isValid()) {
    if (error) {
      *error = q.lastError().text().contains("UNIQUE", Qt::CaseInsensitive)
                 ? QObject::tr("A derived variable named '%1' already exists.").arg(definition.name.trimmed())
                 : q.lastError().text();
    }
    return std::nullopt;
  }
  QSqlQuery row = db.query("SELECT * FROM derived_variables WHERE id = ?", {q.lastInsertId()});
  return row.next() ? std::optional<DerivedVariable>(readRow(row)) : std::nullopt;
}

bool DerivedVariablesStore::remove(qint64 id) {
  ScopedDb db(DERIVED_VARIABLES_DB);
  ensureSchema(db);
  return db.exec("DELETE FROM derived_variables WHERE id = ?", {id});
}

} // namespace deepvaccontrol::storage
