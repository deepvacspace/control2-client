#ifndef DERIVEDVARIABLESSTORE_H
#define DERIVEDVARIABLESSTORE_H

#include <QColor>
#include <QString>
#include <QVector>
#include <optional>

namespace deepvaccontrol::storage {

// Derived (calculated) channel: a reusable formula applied to any experiment that has its sources.
struct DerivedVariable {
  enum class Type : uint8_t {
    Difference,         // A - B
    RateOfChange,       // dA/dt, per second
    RollingStd,         // standard deviation of the last `window` samples
    CumulativeIntegral, // integral of A dt (trapezoids)
    Custom              // expression over channel names, see DerivedExpression
  };

  qint64  id{-1};
  QString name;
  Type    type{Type::Difference};
  QString sourceChannel;
  QString sourceChannel2;
  int     window{0};
  QString expression;
  QColor  color;
  QString createdBy;
  QString createdAt;

  static QString typeKey(Type type); // stored text: "difference", "rate_of_change", ...
  static std::optional<Type> typeFromKey(QString const &key);
};

/**
 * @brief Derived variable definitions in data/deepvac_derived_variables.sqlite3
 * (same schema as Deepvac Insight's derived_variables_service).
 */
class DerivedVariablesStore {
public:
  static QVector<DerivedVariable> list(QString *error = nullptr);
  // validation and error texts as in Insight; returns the stored definition
  static std::optional<DerivedVariable> add(DerivedVariable const &definition, QString *error = nullptr);
  static bool                           remove(qint64 id);
  static bool                           validate(DerivedVariable const &definition, QString *error);
};

}

#endif // DERIVEDVARIABLESSTORE_H
