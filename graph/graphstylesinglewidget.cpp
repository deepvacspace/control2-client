#include "graphstylesinglewidget.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>

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

  m_label = new QLabel(m_stateMap["label"].toString(), this);
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
    QPixmap pix(80 * ssf, 14 * ssf);
    pix.fill(Qt::transparent);

    QPen pen;
    pen.setWidth(m_stateMap["width"].toInt());
    pen.setStyle(static_cast<Qt::PenStyle>(i));
    pen.setColor(QColor::fromString(m_stateMap["color"].toString()));

    QPainter painter(&pix);
    painter.setPen(pen);
    painter.drawLine(2 * ssf, 7 * ssf, 78 * ssf, 7 * ssf);
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
  double const ssf{utils::ssf()};
  m_lineStyle->setFixedSize(static_cast<int>(76 * ssf), static_cast<int>(28 * ssf));
  m_lineStyle->setIconSize(QSize(static_cast<int>(48 * ssf), static_cast<int>(14 * ssf)));
  m_lineWidth->setFixedSize(static_cast<int>(62 * ssf), static_cast<int>(28 * ssf));
  m_label->setWordWrap(true);
  layout()->setSpacing(static_cast<int>(6 * ssf));
}

} // namespace deepvaccontrol
