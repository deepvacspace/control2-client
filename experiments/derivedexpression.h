#ifndef DERIVEDEXPRESSION_H
#define DERIVEDEXPRESSION_H

#include <QHash>
#include <QStringList>
#include <QVector>
#include <memory>

#include "derivedvariablesstore.h"

namespace deepvaccontrol {

/**
 * @brief Restricted arithmetic expression for custom derived variables
 * (e.g. "temp_u_p + temp_u_i + temp_u_d", "sqrt(abs(temp_ref - temp))").
 * Never executes code: numbers, channel names, + - * / // % ** (Python precedence),
 * parentheses and the functions abs min max sqrt log log10 exp sin cos tan clip.
 */
class DerivedExpression {
public:
  struct Node;

  // nullptr and an error text when the expression is not valid
  static std::shared_ptr<DerivedExpression const> parse(QString const &text, QString *error = nullptr);

  [[nodiscard]] QStringList referencedNames() const; // channel names, not function names
  [[nodiscard]] double      evaluate(QHash<QString, QVector<double> const *> const &columns, qsizetype index) const;

  ~DerivedExpression();

private:
  DerivedExpression() = default;
  std::unique_ptr<Node> m_root;
  QStringList           m_names;
};

namespace derived {
// channel names a definition needs from an experiment
QStringList requiredChannels(storage::DerivedVariable const &definition);

// computes the derived series (same length as `elapsed`, seconds since the first sample)
bool compute(storage::DerivedVariable const &definition, QHash<QString, QVector<double> const *> const &columns,
             QVector<double> const &elapsed, QVector<double> &out, QString *error = nullptr);
}

}

#endif // DERIVEDEXPRESSION_H
