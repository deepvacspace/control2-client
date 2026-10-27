#include "derivedexpression.h"

#include <QObject>
#include <cmath>
#include <functional>

namespace deepvaccontrol {

using storage::DerivedVariable;

namespace {
struct Function {
  char const *name;
  int         arity;
};

Function const FUNCTIONS[]{{"abs", 1},  {"min", 2}, {"max", 2}, {"sqrt", 1}, {"log", 1}, {"log10", 1},
                           {"exp", 1},  {"sin", 1}, {"cos", 1}, {"tan", 1},  {"clip", 3}};

int functionIndex(QString const &name) {
  for (int i{0}; i < static_cast<int>(std::size(FUNCTIONS)); ++i) {
    if (name == QLatin1String(FUNCTIONS[i].name)) {
      return i;
    }
  }
  return -1;
}

QString allowedFunctions() {
  QStringList names;
  for (Function const &function : FUNCTIONS) {
    names.push_back(QLatin1String(function.name));
  }
  names.sort();
  return names.join(", ");
}

double pythonFloorDiv(double a, double b) {
  return std::floor(a / b);
}

double pythonMod(double a, double b) {
  // numpy / Python: result has the sign of the divisor
  return (b == 0.0) ? qQNaN() : a - b * std::floor(a / b);
}

double nanMin(double a, double b) { // np.minimum propagates NaN
  return (std::isnan(a) || std::isnan(b)) ? qQNaN() : std::min(a, b);
}

double nanMax(double a, double b) {
  return (std::isnan(a) || std::isnan(b)) ? qQNaN() : std::max(a, b);
}
}

struct DerivedExpression::Node {
  enum class Kind : uint8_t {
    NUMBER,
    VARIABLE,
    UNARY,
    BINARY,
    CALL
  };
  Kind                               kind{Kind::NUMBER};
  double                             number{0.0};
  QString                            name; // variable name, or operator text
  int                                function{-1};
  std::vector<std::unique_ptr<Node>> children;
};

namespace {
using Node = DerivedExpression::Node;

class Parser {
public:
  explicit Parser(QString const &text) : m_text{text} {
  }

  std::unique_ptr<Node> parse() {
    tokenize();
    auto root = expression();
    if (m_pos != m_tokens.size()) {
      fail(QObject::tr("unexpected '%1'").arg(m_tokens[m_pos]));
    }
    return root;
  }

  QStringList names;

private:
  [[noreturn]] void fail(QString const &reason) const {
    throw QObject::tr("Invalid expression syntax: %1").arg(reason);
  }

  void tokenize() {
    qsizetype i{0};
    while (i < m_text.size()) {
      QChar const ch = m_text[i];
      if (ch.isSpace()) {
        ++i;
      } else if (ch.isDigit() || ((ch == '.') && (i + 1 < m_text.size()) && m_text[i + 1].isDigit())) {
        qsizetype j{i};
        while ((j < m_text.size()) && (m_text[j].isDigit() || m_text[j] == '.')) {
          ++j;
        }
        if ((j < m_text.size()) && ((m_text[j] == 'e') || (m_text[j] == 'E'))) {
          qsizetype k{j + 1};
          if ((k < m_text.size()) && ((m_text[k] == '+') || (m_text[k] == '-'))) {
            ++k;
          }
          if ((k < m_text.size()) && m_text[k].isDigit()) {
            j = k;
            while ((j < m_text.size()) && m_text[j].isDigit()) {
              ++j;
            }
          }
        }
        m_tokens.push_back(m_text.mid(i, j - i));
        i = j;
      } else if (ch.isLetter() || (ch == '_')) {
        qsizetype j{i};
        while ((j < m_text.size()) && (m_text[j].isLetterOrNumber() || (m_text[j] == '_'))) {
          ++j;
        }
        m_tokens.push_back(m_text.mid(i, j - i));
        i = j;
      } else if ((m_text.mid(i, 2) == "**") || (m_text.mid(i, 2) == "//")) {
        m_tokens.push_back(m_text.mid(i, 2));
        i += 2;
      } else if (QStringLiteral("+-*/%(),").contains(ch)) {
        m_tokens.push_back(QString(ch));
        ++i;
      } else {
        fail(QObject::tr("unsupported character '%1'").arg(ch));
      }
    }
    if (m_tokens.isEmpty()) {
      fail(QObject::tr("empty expression"));
    }
  }

  [[nodiscard]] QString peek() const {
    return m_pos < m_tokens.size() ? m_tokens[m_pos] : QString{};
  }

  QString take() {
    if (m_pos >= m_tokens.size()) {
      fail(QObject::tr("unexpected end of expression"));
    }
    return m_tokens[m_pos++];
  }

  void expect(QString const &token) {
    if (take() != token) {
      fail(QObject::tr("'%1' expected").arg(token));
    }
  }

  static std::unique_ptr<Node> binary(QString const &op, std::unique_ptr<Node> left, std::unique_ptr<Node> right) {
    auto node  = std::make_unique<Node>();
    node->kind = Node::Kind::BINARY;
    node->name = op;
    node->children.push_back(std::move(left));
    node->children.push_back(std::move(right));
    return node;
  }

  // expression := term (('+' | '-') term)*
  std::unique_ptr<Node> expression() {
    auto node = term();
    while ((peek() == "+") || (peek() == "-")) {
      QString const op = take();
      node             = binary(op, std::move(node), term());
    }
    return node;
  }

  // term := unary (('*' | '/' | '//' | '%') unary)*
  std::unique_ptr<Node> term() {
    auto node = unary();
    while ((peek() == "*") || (peek() == "/") || (peek() == "//") || (peek() == "%")) {
      QString const op = take();
      node             = binary(op, std::move(node), unary());
    }
    return node;
  }

  // unary := ('+' | '-') unary | power      (-2**2 == -4 as in Python)
  std::unique_ptr<Node> unary() {
    if ((peek() == "+") || (peek() == "-")) {
      auto node  = std::make_unique<Node>();
      node->kind = Node::Kind::UNARY;
      node->name = take();
      node->children.push_back(unary());
      return node;
    }
    return power();
  }

  // power := atom ['**' unary]      (right associative)
  std::unique_ptr<Node> power() {
    auto node = atom();
    if (peek() == "**") {
      take();
      node = binary("**", std::move(node), unary());
    }
    return node;
  }

  std::unique_ptr<Node> atom() {
    QString const token = take();
    if (token == "(") {
      auto node = expression();
      expect(")");
      return node;
    }
    if (token[0].isDigit() || token[0] == '.') {
      bool         ok{false};
      double const value{token.toDouble(&ok)};
      if (!ok) {
        fail(QObject::tr("bad number '%1'").arg(token));
      }
      auto node    = std::make_unique<Node>();
      node->kind   = Node::Kind::NUMBER;
      node->number = value;
      return node;
    }
    if (token[0].isLetter() || token[0] == '_') {
      if (peek() == "(") {
        int const index{functionIndex(token)};
        if (index < 0) {
          throw QObject::tr("Unsupported function call -- allowed: %1").arg(allowedFunctions());
        }
        take();
        auto node      = std::make_unique<Node>();
        node->kind     = Node::Kind::CALL;
        node->function = index;
        if (peek() != ")") {
          node->children.push_back(expression());
          while (peek() == ",") {
            take();
            node->children.push_back(expression());
          }
        }
        expect(")");
        if (static_cast<int>(node->children.size()) != FUNCTIONS[index].arity) {
          throw QObject::tr("%1() takes %n argument(s)", "", FUNCTIONS[index].arity).arg(QLatin1String(FUNCTIONS[index].name));
        }
        return node;
      }
      auto node  = std::make_unique<Node>();
      node->kind = Node::Kind::VARIABLE;
      node->name = token;
      if (!names.contains(token)) {
        names.push_back(token);
      }
      return node;
    }
    fail(QObject::tr("unexpected '%1'").arg(token));
  }

  QString     m_text;
  QStringList m_tokens;
  qsizetype   m_pos{0};
};

double evaluateNode(Node const &node, QHash<QString, QVector<double> const *> const &columns, qsizetype index) {
  switch (node.kind) {
    case Node::Kind::NUMBER:
      return node.number;
    case Node::Kind::VARIABLE: {
      QVector<double> const *column = columns.value(node.name, nullptr);
      return (column && (index < column->size())) ? column->at(index) : qQNaN();
    }
    case Node::Kind::UNARY: {
      double const value{evaluateNode(*node.children[0], columns, index)};
      return node.name == "-" ? -value : value;
    }
    case Node::Kind::BINARY: {
      double const a{evaluateNode(*node.children[0], columns, index)};
      double const b{evaluateNode(*node.children[1], columns, index)};
      if (node.name == "+") {
        return a + b;
      }
      if (node.name == "-") {
        return a - b;
      }
      if (node.name == "*") {
        return a * b;
      }
      if (node.name == "/") {
        return a / b;
      }
      if (node.name == "//") {
        return pythonFloorDiv(a, b);
      }
      if (node.name == "%") {
        return pythonMod(a, b);
      }
      return std::pow(a, b);
    }
    case Node::Kind::CALL: {
      auto arg = [&](int i) {
        return evaluateNode(*node.children[i], columns, index);
      };
      switch (node.function) {
        case 0:
          return std::fabs(arg(0));
        case 1:
          return nanMin(arg(0), arg(1));
        case 2:
          return nanMax(arg(0), arg(1));
        case 3:
          return std::sqrt(arg(0));
        case 4:
          return std::log(arg(0));
        case 5:
          return std::log10(arg(0));
        case 6:
          return std::exp(arg(0));
        case 7:
          return std::sin(arg(0));
        case 8:
          return std::cos(arg(0));
        case 9:
          return std::tan(arg(0));
        default:
          return nanMin(nanMax(arg(0), arg(1)), arg(2)); // clip(x, lo, hi)
      }
    }
  }
  return qQNaN();
}
}

DerivedExpression::~DerivedExpression() = default;

std::shared_ptr<DerivedExpression const> DerivedExpression::parse(QString const &text, QString *error) {
  try {
    Parser parser(text);
    auto   expression = std::shared_ptr<DerivedExpression>(new DerivedExpression());
    expression->m_root  = parser.parse();
    expression->m_names = parser.names;
    return expression;
  } catch (QString const &message) {
    if (error) {
      *error = message;
    }
    return nullptr;
  }
}

QStringList DerivedExpression::referencedNames() const {
  return m_names;
}

double DerivedExpression::evaluate(QHash<QString, QVector<double> const *> const &columns, qsizetype index) const {
  return evaluateNode(*m_root, columns, index);
}

namespace derived {

QStringList requiredChannels(DerivedVariable const &definition) {
  switch (definition.type) {
    case DerivedVariable::Type::Difference:
      return {definition.sourceChannel, definition.sourceChannel2};
    case DerivedVariable::Type::Custom: {
      auto const expression = DerivedExpression::parse(definition.expression);
      return expression ? expression->referencedNames() : QStringList{};
    }
    default:
      return {definition.sourceChannel};
  }
}

bool compute(DerivedVariable const &definition, QHash<QString, QVector<double> const *> const &columns,
             QVector<double> const &elapsed, QVector<double> &out, QString *error) {
  QStringList missing;
  for (QString const &name : requiredChannels(definition)) {
    if (!columns.contains(name)) {
      missing.push_back(name);
    }
  }
  if (!missing.isEmpty()) {
    if (error) {
      *error = QObject::tr("'%1' needs channel(s) not present in this experiment: %2").arg(definition.name, missing.join(", "));
    }
    return false;
  }

  qsizetype const n{elapsed.size()};
  out = QVector<double>(n, qQNaN());
  auto value = [&columns](QString const &name, qsizetype i) {
    QVector<double> const *column = columns.value(name);
    return (i < column->size()) ? column->at(i) : qQNaN();
  };

  switch (definition.type) {
    case DerivedVariable::Type::Difference:
      for (qsizetype i{0}; i < n; ++i) {
        out[i] = value(definition.sourceChannel, i) - value(definition.sourceChannel2, i);
      }
      break;

    case DerivedVariable::Type::RateOfChange:
      // dv/dt with the previous sample (first sample: 0 change over 1 s); dt == 0 gives NaN
      for (qsizetype i{0}; i < n; ++i) {
        double const dt{i == 0 ? 1.0 : elapsed[i] - elapsed[i - 1]};
        double const dv{i == 0 ? 0.0 : value(definition.sourceChannel, i) - value(definition.sourceChannel, i - 1)};
        out[i] = (dt == 0.0) ? qQNaN() : dv / dt;
      }
      break;

    case DerivedVariable::Type::RollingStd: {
      // sample standard deviation (ddof = 1) of the last `window` valid samples
      int const window{std::max(2, definition.window)};
      for (qsizetype i{0}; i < n; ++i) {
        double    sum{0.0};
        double    sumSquares{0.0};
        qsizetype count{0};
        for (qsizetype j{std::max<qsizetype>(0, i - window + 1)}; j <= i; ++j) {
          double const v{value(definition.sourceChannel, j)};
          if (!std::isnan(v)) {
            sum += v;
            sumSquares += v * v;
            ++count;
          }
        }
        if (count >= 2) {
          double const mean{sum / count};
          out[i] = std::sqrt(std::max(0.0, (sumSquares - count * mean * mean) / (count - 1)));
        }
      }
      break;
    }

    case DerivedVariable::Type::CumulativeIntegral: {
      // trapezoids, missing values count as 0
      double total{0.0};
      for (qsizetype i{0}; i < n; ++i) {
        if (i > 0) {
          double a{value(definition.sourceChannel, i - 1)};
          double b{value(definition.sourceChannel, i)};
          a = std::isnan(a) ? 0.0 : a;
          b = std::isnan(b) ? 0.0 : b;
          total += (elapsed[i] - elapsed[i - 1]) * (a + b) / 2.0;
        }
        out[i] = total;
      }
      break;
    }

    case DerivedVariable::Type::Custom: {
      QString    parseError;
      auto const expression = DerivedExpression::parse(definition.expression, &parseError);
      if (!expression) {
        if (error) {
          *error = parseError;
        }
        return false;
      }
      for (qsizetype i{0}; i < n; ++i) {
        out[i] = expression->evaluate(columns, i);
      }
      break;
    }
  }

  // inf (e.g. division by zero) cannot be plotted
  for (double &v : out) {
    if (std::isinf(v)) {
      v = qQNaN();
    }
  }
  return true;
}

}

}
