#include "graphstylesinglewidget.h"

#include <QColorDialog>
#include <QGridLayout>
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
  setObjectName("outputRow");
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(theme::dp(20), theme::dp(8), theme::dp(20), theme::dp(8));
  mainLay->setSpacing(theme::dp(20));

  // shown on the chart
  m_visibility = new QCheckBox(this);
  m_visibility->setToolTip(tr("Show this channel on the chart"));

  // colour swatch: opens the colour picker
  m_colorPicker = new QPushButton(this);
  m_colorPicker->setFixedSize(theme::dp(48), theme::dp(48));
  m_colorPicker->setToolTip(tr("Line colour"));

  // the pressure as the charts draw it, in Torr
  QString const label = m_stateMap["label"].toString();
  m_label = new QLabel(pressure::isPressureChannel(m_stateMap["name"].toString()) ? pressure::torrLabel(label) : label, this);
  m_label->setToolTip(m_stateMap["name"].toString());

  m_lineStyle = new QComboBox(this);
  m_lineStyle->addItems(QStringList{"", "", "", "", ""});
  m_lineStyle->setFixedSize(theme::dp(206), theme::dp(52));
  m_lineStyle->setIconSize(QSize(theme::dp(150), theme::dp(26)));
  m_lineStyle->setToolTip(tr("Line style"));

  m_lineWidth = new QComboBox(this);
  m_lineWidth->addItems(QStringList{"1 px", "2 px", "3 px", "4 px"});
  m_lineWidth->setFixedSize(theme::dp(135), theme::dp(52));
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
  if (m_styleBox) { // compact: the line boxes only while the channel is shown
    m_styleBox->setVisible(value);
  }
  updateSwatch();
}

void GraphStyleSingleWidget::setStateMap(QVariantMap const &stateMap) {
  QSignalBlocker const styleBlock(m_lineStyle);
  QSignalBlocker const widthBlock(m_lineWidth);
  for (char const *field : {"color", "pen", "width"}) {
    m_stateMap[field] = stateMap.value(field);
  }
  m_lineWidth->setCurrentIndex(m_stateMap["width"].toInt() - 1);
  updateLineStyleBox();
  m_lineStyle->setCurrentIndex(m_stateMap["pen"].toInt() - 1);
  setEnabled(stateMap.value("visible").toBool());
}

void GraphStyleSingleWidget::setCompact() {
  // a card: shown, colour and the name (the whole width left, wrapped); under the name, while shown, small boxes for
  // the line style (a short sample) and width (beside the name they leave it no room in the narrow side column)
  setObjectName("channelRow");
  delete layout(); // its widgets stay, laid out again below
  auto *grid = new QGridLayout(this);
  grid->setContentsMargins(theme::dp(16), theme::dp(12), theme::dp(16), theme::dp(12));
  grid->setHorizontalSpacing(theme::dp(14));
  grid->setVerticalSpacing(theme::dp(8));
  m_colorPicker->setFixedSize(theme::dp(41), theme::dp(41));
  m_lineSample = 22;
  m_lineStyle->setIconSize(QSize(qRound(m_lineSample * utils::ssf()), theme::dp(26)));
  m_lineStyle->setFixedSize(theme::dp(82), theme::dp(48));
  m_lineWidth->setFixedSize(theme::dp(101), theme::dp(48));
  m_visibility->setFixedSize(theme::dp(41), theme::dp(41));
  QFont small = m_label->font();
  small.setPointSizeF(small.pointSizeF() * 0.9);
  m_label->setFont(small);
  m_label->setWordWrap(true);
  m_label->setMinimumWidth(0);
  m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_styleBox    = new QWidget(this);
  auto *boxLay  = new QHBoxLayout(m_styleBox);
  boxLay->setContentsMargins(0, 0, 0, 0);
  boxLay->setSpacing(theme::dp(12));
  boxLay->addWidget(m_lineStyle);
  boxLay->addWidget(m_lineWidth);
  boxLay->addStretch();
  grid->addWidget(m_visibility, 0, 0);
  grid->addWidget(m_colorPicker, 0, 1);
  grid->addWidget(m_label, 0, 2);
  grid->addWidget(m_styleBox, 1, 2);
  grid->setColumnStretch(2, 1);
  m_styleBox->setVisible(m_stateMap["visible"].toBool());
  int const pen{m_lineStyle->currentIndex()};
  updateLineStyleBox();
  m_lineStyle->setCurrentIndex(pen);
  style()->unpolish(this);
  style()->polish(this);
}

}
