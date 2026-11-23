#include "pidwidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>
#include <cmath>

#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
PidWidget::PidWidget(QVariantMap data, QWidget *parent) : QWidget{parent} {
  m_name     = data["name"].toString();
  m_label    = data["label"].toString();
  m_points   = data["points"].toUInt();
  m_minPoint = data["min"].toFloat();
  m_maxPoint = data["max"].toFloat();

  initGui();
  setDataVariant(data);
}

void PidWidget::setDataVariant(QVariantMap data) {
  if (data["points"].toUInt() != m_points) {
    return;
  }

  auto lineEditIter = m_fields.begin();
  // P
  for (quint32 i{0U}; i < m_points; ++i) {
    (*lineEditIter)->setValue(data["p"].toList()[i].toFloat());
    ++lineEditIter;
  }
  // I
  for (quint32 i{0U}; i < m_points; ++i) {
    (*lineEditIter)->setValue(data["i"].toList()[i].toFloat());
    ++lineEditIter;
  }
  // D
  for (quint32 i{0U}; i < m_points; ++i) {
    (*lineEditIter)->setValue(data["d"].toList()[i].toFloat());
    ++lineEditIter;
  }
}

QVariantMap PidWidget::getDataVariant() const {
  QVariantMap ret;

  ret["name"]   = m_name;
  ret["label"]  = m_label;
  ret["points"] = m_points;
  ret["min"]    = m_minPoint;
  ret["max"]    = m_maxPoint;

  auto lineEditIter = m_fields.begin();

  // P
  QVariantList pList;
  for (quint32 i{0U}; i < m_points; ++i) {
    pList.push_back((*lineEditIter)->text().toFloat());
    ++lineEditIter;
  }
  // I
  QVariantList iList;
  for (quint32 i{0U}; i < m_points; ++i) {
    iList.push_back((*lineEditIter)->text().toFloat());
    ++lineEditIter;
  }
  // D
  QVariantList dList;
  for (quint32 i{0U}; i < m_points; ++i) {
    dList.push_back((*lineEditIter)->text().toFloat());
    ++lineEditIter;
  }

  ret["p"] = pList;
  ret["i"] = iList;
  ret["d"] = dList;

  return ret;
}

void PidWidget::initGui() {
  if (0U == m_points) {
    return;
  }
  auto        *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, theme::dp(16), 0, 0);
  mainLay->setSpacing(theme::dp(20));

  // ---- gains per band
  auto *tableCard = new QFrame(this);
  tableCard->setObjectName("pageCard");
  auto *tableLay = new QVBoxLayout(tableCard);
  tableLay->setContentsMargins(theme::dp(28), theme::dp(20), theme::dp(28), theme::dp(24));
  tableLay->setSpacing(theme::dp(8));
  auto *top   = new QHBoxLayout();
  auto *title = new QLabel(tr("Temperature PID coefficients"), tableCard);
  title->setObjectName("cardTitle");
  top->addWidget(title);
  top->addStretch();
  tableLay->addLayout(top);
  tableLay->addSpacing(theme::dp(12));

  auto addRow = [&](QFrame *row) {
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(theme::dp(20), theme::dp(8), theme::dp(20), theme::dp(8));
    grid->setHorizontalSpacing(theme::dp(24));
    for (int column : {0, 1, 2, 3, 4}) {
      grid->setColumnStretch(column, column == 4 ? 3 : 2);
    }
    tableLay->addWidget(row);
    return grid;
  };
  auto *header = new QFrame(tableCard);
  header->setObjectName("bandHeader");
  auto             *headerGrid = addRow(header);
  QStringList const titles{tr("Band"), tr("P"), tr("I"), tr("D"), tr("Range (°C)")};
  for (int column{0}; column < titles.size(); ++column) {
    auto *label = new QLabel(titles[column], header);
    label->setProperty("muted", true);
    headerGrid->addWidget(label, 0, column);
  }

  float const            rangeWidth{(m_maxPoint - m_minPoint) / m_points};
  QVector<QGridLayout *> grids(static_cast<int>(m_points));
  m_rows.resize(static_cast<int>(m_points));
  // highest range on top, like the scale
  for (int i{static_cast<int>(m_points) - 1}; i >= 0; --i) {
    auto *row = new QFrame(tableCard);
    row->setObjectName("bandRow");
    m_rows[i]  = row;
    grids[i]   = addRow(row);
    auto *name = new QLabel(tr("Band %1").arg(m_points - i), row);
    QFont bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    grids[i]->addWidget(name, 0, 0);
    double const start{m_minPoint + i * rangeWidth};
    double const end{m_minPoint + (i + 1) * rangeWidth};
    auto *range = new QLabel(QStringLiteral("%1 - %2").arg(start).arg(end).replace('-', QChar(0x2212)), row);
    range->setProperty("muted", true);
    grids[i]->addWidget(range, 0, 4);
  }
  // fields in the stored order: all P (lowest band first), then all I, then all D
  struct Gain {
    double min, max;
    int    prec;
  };
  QList<Gain> const gains{{0, 50, 0}, {0, 1000, 0}, {0, 20, 3}};
  for (int g{0}; g < gains.size(); ++g) {
    for (quint32 i{0U}; i < m_points; ++i) {
      auto *lineEdit = new LineEditNum(gains[g].min, gains[g].max, gains[g].prec, false, m_rows[i]);
      lineEdit->setFont(QWidget::font()); // the page's font
      lineEdit->setMinimumWidth(theme::dp(131));
      lineEdit->setMaximumWidth(QWIDGETSIZE_MAX);
      lineEdit->setFixedHeight(theme::dp(56));
      grids[i]->addWidget(lineEdit, 0, 1 + g);
      m_fields.push_back(lineEdit);
    }
  }
  tableLay->addStretch();
  mainLay->addWidget(tableCard, 1);

}

void PidWidget::setSetpointValue(SystemValue *value) {
  m_setpoint = value;
  if (m_setpoint) {
    connect(m_setpoint, &SystemValue::valueChanged, this, &PidWidget::updateActiveBand);
  }
  updateActiveBand();
}

void PidWidget::setCurrentValue(SystemValue *value) {
  m_current = value;
  if (m_current) {
    connect(m_current, &SystemValue::valueChanged, this, &PidWidget::updateActiveBand);
  }
  updateActiveBand();
}

void PidWidget::updateActiveBand() {
  if (m_rows.isEmpty() || (m_points == 0U)) {
    return;
  }
  auto read = [](SystemValue const *state) {
    double const v = state ? static_cast<double>(state->getValue()) : std::numeric_limits<double>::quiet_NaN();
    return (std::isnan(v) || (std::fabs(v) > 1e9)) ? std::numeric_limits<double>::quiet_NaN() : v;
  };
  double const temperature = read(m_current);
  double const setpoint    = read(m_setpoint);
  // the PLC selects the band from the setpoint
  int active{-1};
  if (!std::isnan(setpoint)) {
    float const width{(m_maxPoint - m_minPoint) / m_points};
    active = std::clamp(static_cast<int>(std::floor((setpoint - m_minPoint) / width)), 0, static_cast<int>(m_points) - 1);
  }
  for (int i{0}; i < m_rows.size(); ++i) {
    bool const isActive{i == active};
    if (m_rows[i]->property("active").toBool() != isActive) {
      m_rows[i]->setProperty("active", isActive);
      m_rows[i]->style()->unpolish(m_rows[i]);
      m_rows[i]->style()->polish(m_rows[i]);
    }
  }
  Q_UNUSED(temperature)
}
}
