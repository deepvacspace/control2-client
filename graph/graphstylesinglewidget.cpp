#include "graphstylesinglewidget.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>

#include "utils.h"

namespace deepvaccontrol {

GraphStyleSingleWidget::GraphStyleSingleWidget(int index, QVariantMap const &stateMap, QWidget *parent)
    : QWidget{parent}, m_index{index}, m_stateMap{stateMap} {
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  m_visibility = new QCheckBox(this);
  m_visibility->setStyleSheet(utils::readStyleFromFile(":/styles/checkBoxBig.qss")
                              + QString("QCheckBox::indicator{width: %1px; height: %1px;}").arg(26 * utils::ssf()));

  auto *label = new QLabel(this);
  // label->setFixedWidth(250 * utils::ssf());

  m_colorPicker = new QPushButton(tr("Цвет"), this);
  m_colorPicker->setFixedSize(40 * utils::ssf(), 20 * utils::ssf());

  m_lineStyle = new QComboBox(this);
  m_lineWidth = new QComboBox(this);

  label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  m_lineWidth->addItems(QStringList{"1", "2", "3", "4"});
  m_lineWidth->setFixedSize(35 * utils::ssf(), 20 * utils::ssf());
  m_lineWidth->setStyleSheet("QComboBox QAbstractItemView {selection-color: black;}");

  m_lineStyle->addItems(QStringList{"", "", "", "", ""});
  m_lineStyle->setFixedSize(88 * utils::ssf(), 20 * utils::ssf());
  m_lineStyle->setIconSize(QSize(80 * utils::ssf(), 15 * utils::ssf()));

  mainLay->addWidget(m_visibility);
  mainLay->addWidget(label);
  mainLay->addWidget(m_colorPicker);
  mainLay->addWidget(m_lineStyle);
  mainLay->addWidget(m_lineWidth);

  label->setText(m_stateMap["label"].toString());
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
  for (int i = Qt::SolidLine; i < Qt::CustomDashLine; ++i) {
    QPixmap pix(80 * utils::ssf(), 15 * utils::ssf());
    pix.fill(Qt::transparent);

    QPen pen;
    pen.setWidth(m_stateMap["width"].toInt());
    pen.setStyle(static_cast<Qt::PenStyle>(i));
    pen.setColor(QColor::fromString(m_stateMap["color"].toString()));

    QPainter painter(&pix);
    painter.setPen(pen);
    painter.drawLine(2 * utils::ssf(), 8 * utils::ssf(), 78 * utils::ssf(), 8 * utils::ssf());
    m_lineStyle->setItemIcon(i - 1, QIcon(pix));
  }
  m_colorPicker->setStyleSheet("QPushButton {color :" + m_stateMap["color"].toString() + ";}");
}

void GraphStyleSingleWidget::setEnabled(bool value) {
  // if(m_visibility->isEnabled() == value) {
  //   return;
  // }
  m_stateMap["visible"] = value;

  m_colorPicker->setEnabled(value);
  m_lineStyle->setEnabled(value);
  m_lineWidth->setEnabled(value);
  m_visibility->setChecked(value);

  static QColor const grey(127, 127, 127);
  m_colorPicker->setStyleSheet("QPushButton {color :" + (value ? m_stateMap["color"].toString() : grey.name()) + ";}");
}

} // namespace deepvaccontrol
