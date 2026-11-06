#include "graphstylesinglewidget.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>

#include "pressureunits.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

GraphStyleSingleWidget::GraphStyleSingleWidget(int index, QVariantMap const &stateMap, QWidget *parent)
    : QFrame{parent}, m_index{index}, m_stateMap{stateMap} {
  double const ssf{utils::ssf()};
  setObjectName("outputRow");
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(10 * ssf, 5 * ssf, 10 * ssf, 5 * ssf);
  mainLay->setSpacing(10 * ssf);

  // shown on the chart
  m_visibility = new QCheckBox(this);
  m_visibility->setToolTip(tr("Show this channel on the chart"));

  // colour swatch: opens the colour picker
  m_colorPicker = new QPushButton(this);
  m_colorPicker->setFixedSize(26 * ssf, 26 * ssf);
  m_colorPicker->setToolTip(tr("Line colour"));

  // the pressure as the charts draw it, in Torr
  QString const label = m_stateMap["label"].toString();
  m_label = new QLabel(pressure::isPressureChannel(m_stateMap["name"].toString()) ? pressure::torrLabel(label) : label, this);
  m_label->setToolTip(m_stateMap["name"].toString());

  m_lineStyle = new QComboBox(this);
  m_lineStyle->addItems(QStringList{"", "", "", "", ""});
  m_lineStyle->setFixedSize(110 * ssf, 28 * ssf);
  m_lineStyle->setIconSize(QSize(80 * ssf, 14 * ssf));
  m_lineStyle->setToolTip(tr("Line style"));

  m_lineWidth = new QComboBox(this);
  m_lineWidth->addItems(QStringList{"1 px", "2 px", "3 px", "4 px"});
  m_lineWidth->setFixedSize(72 * ssf, 28 * ssf);
  m_lineWidth->setToolTip(tr("Line width"));

  mainLay->addWidget(m_visibility);
  mainLay->addWidget(m_colorPicker);
  mainLay->addWidget(m_label, 1);
  mainLay->addWidget(m_lineStyle);
  mainLay->addWidget(m_lineWidth);

  updateLineStyleBox();
  setEnabled(m_stateMap["visible"].toBool());
  m_lineWidth->setCurrentIndex(m_stateMap["width"].toUInt() - 1);
  m_lineStyle->setCurrentIndex(m_stateMap["pen"].toUInt() - 1);

  connect(m_colorPicker, &QPushButton::clicked, this, [this]() {
    auto *colorDialog = new QColorDialog(QColor::fromString(m_stateMap["color"].toString()), this);
    colorDialog->open(this, SLOT(handleColorSelected(QColor)));
  });

  connect(m_visibility, &QCheckBox::clicked, [this](bool value) {
    setEnabled(value);
    emit styleUpdated();
  });

  connect(m_lineStyle, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this](int index) {
    m_stateMap["pen"] = index + 1;
    emit styleUpdated();
  });

  connect(m_lineWidth, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this](int index) {
    m_stateMap["width"] = index + 1;
    int const tmp{m_lineStyle->currentIndex()};
    updateLineStyleBox();
    m_lineStyle->setCurrentIndex(tmp);
    emit styleUpdated();
  });
}

void GraphStyleSingleWidget::handleColorSelected(QColor newColor) {
  if (newColor == QColor::fromString(m_stateMap["color"].toString())) {
    return;
  }

  m_stateMap["color"] = newColor.name(QColor::NameFormat::HexRgb);
  updateLineStyleBox();
  emit styleUpdated();
}

void GraphStyleSingleWidget::updateLineStyleBox() {
  double const ssf{utils::ssf()};
  for (int i = Qt::SolidLine; i < Qt::CustomDashLine; ++i) {
    QPixmap pix(m_lineSample * ssf, 14 * ssf);
    pix.fill(Qt::transparent);

    QPen pen;
    pen.setWidth(m_stateMap["width"].toInt());
    pen.setStyle(static_cast<Qt::PenStyle>(i));
    pen.setColor(QColor::fromString(m_stateMap["color"].toString()));

    QPainter painter(&pix);
    painter.setPen(pen);
    painter.drawLine(2 * ssf, 7 * ssf, (m_lineSample - 2) * ssf, 7 * ssf);
    m_lineStyle->setItemIcon(i - 1, QIcon(pix));
  }
  updateSwatch();
}

void GraphStyleSingleWidget::updateSwatch() {
  bool const   shown{m_stateMap["visible"].toBool()};
  QColor const color = shown ? QColor::fromString(m_stateMap["color"].toString()) : theme::colors().off;
  m_colorPicker->setStyleSheet(QStringLiteral("QPushButton { background: %1; border: %2px solid %3; border-radius: %4px; }")
                                 .arg(color.name())
                                 .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                                 .arg(theme::colors().border.name())
                                 .arg(static_cast<int>(5 * utils::ssf())));
}

void GraphStyleSingleWidget::setEnabled(bool value) {
  m_stateMap["visible"] = value;

  m_colorPicker->setEnabled(value);
  m_lineStyle->setEnabled(value);
  m_lineWidth->setEnabled(value);
  m_visibility->setChecked(value);
  m_label->setProperty("muted", !value);
  m_label->style()->unpolish(m_label);
  m_label->style()->polish(m_label);
  updateSwatch();
}

void GraphStyleSingleWidget::setCompact() {
  // one line, a card: shown, colour, the name (wrapped), then small boxes for the line style (a short sample) and width
  double const ssf{utils::ssf()};
  auto         px = [ssf](double value) { return static_cast<int>(value * ssf); };
  setObjectName("channelRow");
  delete layout(); // its widgets stay, laid out again below
  auto *lay = new QHBoxLayout(this);
  lay->setContentsMargins(px(10), px(8), px(8), px(8));
  lay->setSpacing(px(8));
  m_colorPicker->setFixedSize(px(22), px(22));
  m_lineSample = 22;
  m_lineStyle->setIconSize(QSize(px(m_lineSample), px(14)));
  m_lineStyle->setFixedSize(px(44), px(30));
  m_lineWidth->setFixedSize(px(54), px(30));
  m_visibility->setFixedSize(px(22), px(22));
  QFont small = m_label->font();
  small.setPointSizeF(small.pointSizeF() * 0.9);
  m_label->setFont(small);
  m_label->setWordWrap(true);
  m_label->setMinimumWidth(0);
  m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  lay->addWidget(m_visibility);
  lay->addWidget(m_colorPicker);
  lay->addWidget(m_label, 1);
  lay->addWidget(m_lineStyle);
  lay->addWidget(m_lineWidth);
  int const pen{m_lineStyle->currentIndex()};
  updateLineStyleBox();
  m_lineStyle->setCurrentIndex(pen);
  style()->unpolish(this);
  style()->polish(this);
}

}
