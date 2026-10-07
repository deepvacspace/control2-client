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

// Bands from the highest range (top) to the lowest, with arrows for the setpoint and the current temperature.
class BandScale : public QWidget {
public:
  BandScale(float minimum, float maximum, quint32 points, QWidget *parent)
      : QWidget{parent}, m_min{minimum}, m_max{maximum}, m_points{points} {
    setMinimumWidth(static_cast<int>(220 * utils::ssf()));
  }
  void setValues(double temperature, double setpoint, int activeBand) {
    m_value    = temperature;
    m_setpoint = setpoint;
    m_active   = activeBand;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    if (m_points == 0) {
      return;
    }
    theme::Colors const &c = theme::colors();
    double const         s{utils::ssf()};
    double const         w{static_cast<double>(width())};
    QPainter             p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF const bar(48 * s, 10 * s, 36 * s, height() - 20 * s);
    double const bandH = bar.height() / m_points;
    auto         yOf   = [&](double v) { return bar.bottom() - (v - m_min) / (m_max - m_min) * bar.height(); };
    QFont        small = font();
    small.setPointSizeF(font().pointSizeF() * 0.8);
    for (quint32 i{0U}; i < m_points; ++i) {
      // i = 0 is the lowest range; hue from blue (cold) to red (hot)
      int const    hue{static_cast<int>(200 - 200.0 * i / qMax<quint32>(1, m_points - 1))};
      bool const   active{static_cast<int>(i) == m_active};
      QColor const color = QColor::fromHsv(hue, active ? 170 : 120, active ? 235 : 120);
      QRectF const band(bar.left(), bar.bottom() - (i + 1) * bandH + 1 * s, bar.width(), bandH - 2 * s);
      p.setPen(Qt::NoPen);
      p.setBrush(color);
      p.drawRoundedRect(band, 3 * s, 3 * s);
      p.setPen(active ? c.text : c.muted);
      QFont nameFont = font();
      nameFont.setBold(active);
      p.setFont(nameFont);
      p.drawText(QRectF(bar.right() + 24 * s, band.top(), 120 * s, band.height()), Qt::AlignVCenter,
                 PidWidget::tr("Band %1").arg(m_points - i));
    }
    // boundaries
    p.setFont(small);
    p.setPen(c.muted);
    float const bandWidth{(m_max - m_min) / m_points};
    for (quint32 i{0U}; i <= m_points; ++i) {
      double const v = m_min + i * bandWidth;
      p.drawText(QRectF(0, yOf(v) - 8 * s, bar.left() - 6 * s, 16 * s), Qt::AlignVCenter | Qt::AlignRight,
                 QString::number(v).replace('-', QChar(0x2212)));
    }
    // arrows pointing at the bar: setpoint (selects the band) and current temperature
    struct Marker {
      double  value;
      QString text;
      QColor  color;
    };
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    double const arrowX = bar.right() + 6 * s;
    double const labelX = std::max(w * 0.55, bar.right() + 110 * s); // right of the band names
    double const labelH = 18 * s;
    QList<Marker> markers;
    if (!std::isnan(m_setpoint)) {
      markers.push_back({m_setpoint, PidWidget::tr("Setpoint %1 °C"), c.accent});
    }
    if (!std::isnan(m_value)) {
      markers.push_back({m_value, PidWidget::tr("Now %1 °C"), c.text});
    }
    double lastLabelY = -1e9;
    std::sort(markers.begin(), markers.end(), [](Marker const &a, Marker const &b) { return a.value > b.value; });
    for (Marker const &marker : std::as_const(markers)) {
      double const y = yOf(std::clamp(marker.value, static_cast<double>(m_min), static_cast<double>(m_max)));
      // keep the two labels apart when the values are close
      double const labelY = std::max(y, lastLabelY + labelH + 2 * s);
      lastLabelY          = labelY;
      p.setPen(QPen(marker.color, 1.5 * s));
      p.drawLine(QPointF(bar.left(), y), QPointF(bar.right(), y));
      p.setPen(Qt::NoPen);
      p.setBrush(marker.color);
      p.drawPolygon(QPolygonF({QPointF(arrowX, y), QPointF(arrowX + 12 * s, y - 7 * s), QPointF(arrowX + 12 * s, y + 7 * s)}));
      p.setPen(QPen(marker.color, 1.2 * s));
      p.drawLine(QPointF(arrowX + 12 * s, y), QPointF(labelX - 6 * s, labelY));
      p.drawText(QRectF(labelX, labelY - labelH / 2, w - labelX - 6 * s, labelH), Qt::AlignLeft | Qt::AlignVCenter,
                 marker.text.arg(QString::number(marker.value, 'f', 1).replace('-', QChar(0x2212))));
    }
  }

private:
  float   m_min;
  float   m_max;
  quint32 m_points;
  double  m_value{std::numeric_limits<double>::quiet_NaN()};
  double  m_setpoint{std::numeric_limits<double>::quiet_NaN()};
  int     m_active{-1};
};

void PidWidget::initGui() {
  if (0U == m_points) {
    return;
  }
  double const ssf{utils::ssf()};
  auto        *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 8 * ssf, 0, 0);
  mainLay->setSpacing(10 * ssf);

  // ---- gains per band
  auto *tableCard = new QFrame(this);
  tableCard->setObjectName("pageCard");
  auto *tableLay = new QVBoxLayout(tableCard);
  tableLay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  tableLay->setSpacing(4 * ssf);
  auto *top   = new QHBoxLayout();
  auto *title = new QLabel(tr("Temperature PID coefficients"), tableCard);
  title->setObjectName("cardTitle");
  top->addWidget(title);
  top->addStretch();
  tableLay->addLayout(top);
  tableLay->addSpacing(6 * ssf);

  auto addRow = [&](QFrame *row) {
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(10 * ssf, 4 * ssf, 10 * ssf, 4 * ssf);
    grid->setHorizontalSpacing(12 * ssf);
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
  QList<Gain> const gains{{0, 50, 1}, {0, 1000, 0}, {0, 20, 3}};
  for (int g{0}; g < gains.size(); ++g) {
    for (quint32 i{0U}; i < m_points; ++i) {
      auto *lineEdit = new LineEditNum(gains[g].min, gains[g].max, gains[g].prec, false, m_rows[i]);
      QFont font     = lineEdit->font();
      font.setPointSizeF(QWidget::font().pointSizeF() * 1.15);
      lineEdit->setFont(font);
      lineEdit->setMinimumWidth(70 * ssf);
      lineEdit->setMaximumWidth(QWIDGETSIZE_MAX);
      lineEdit->setFixedHeight(30 * ssf);
      grids[i]->addWidget(lineEdit, 0, 1 + g);
      m_fields.push_back(lineEdit);
    }
  }
  tableLay->addStretch();
  mainLay->addWidget(tableCard, 3);

  // ---- band scale
  auto *scaleCard = new QFrame(this);
  scaleCard->setObjectName("pageCard");
  auto *scaleLay = new QVBoxLayout(scaleCard);
  scaleLay->setContentsMargins(14 * ssf, 10 * ssf, 14 * ssf, 12 * ssf);
  auto *scaleTitle = new QLabel(tr("Bands"), scaleCard);
  scaleTitle->setObjectName("cardTitle");
  scaleLay->addWidget(scaleTitle);
  m_scale = new BandScale(m_minPoint, m_maxPoint, m_points, scaleCard);
  scaleLay->addWidget(m_scale, 1);
  mainLay->addWidget(scaleCard, 2);

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
  if (!m_scale || (m_points == 0U)) {
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
  m_scale->setValues(temperature, setpoint, active);
}
} // namespace deepvaccontrol
