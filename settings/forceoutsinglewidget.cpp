#include "forceoutsinglewidget.h"

#include <QCheckBox>
#include <QVBoxLayout>

#include "utils.h"

namespace control2 {
ForceOutSingleWidget::ForceOutSingleWidget(
  int index, QString const &stateLabel, SystemValue *sysValue, ForceType type, QWidget *parent)
    : QWidget{parent}, m_index{index} {
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  auto *label = new QLabel(stateLabel, this);
  label->setFixedWidth(200);

  m_valueLabel = new QLabel(" ", this);
  {
    QFont font("Monospace");
    font.setBold(true);
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(20 * utils::ssf());
    m_valueLabel->setFont(font);
  }

  auto *indicator = new QCheckBox(this);
  indicator->setEnabled(false);
  indicator->setStyleSheet("QCheckBox::indicator:unchecked{image: url(:/imgs/check_01.png);}"
                           "QCheckBox::indicator:checked{image: url(:/imgs/check_11.png);}");

  m_slider = new QSlider(this);
  m_slider->setValue(0);
  m_slider->setMinimum(0);
  m_slider->setMaximum((type == ForceType::DUTY ? 3 : 2));
  m_slider->setSingleStep(1);
  m_slider->setPageStep(1);
  m_slider->setOrientation(Qt::Horizontal);
  m_slider->setTickPosition(QSlider::TicksBelow);
  m_slider->setTickInterval(1);
  m_slider->setFixedWidth(m_slider->maximum() * 75 * utils::ssf() + 40);

  auto *bottomLay = new QHBoxLayout();
  bottomLay->setContentsMargins(0, 0, 0, 0);
  mainLay->addWidget(label);
  mainLay->addLayout(bottomLay);

  bottomLay->addWidget(indicator);
  bottomLay->addWidget(m_slider);
  bottomLay->addWidget(m_valueLabel);

  m_setBtn = new QPushButton(tr("Установить"), this);
  m_setBtn->setFixedHeight(35 * utils::ssf());
  resetSetBtn();

  if (type == ForceType::DUTY) {
    duty   = new LineEditNum(0, 1, 3, true, this);
    period = new LineEditNum(0, 600, 1, true, this);
    duty->setText("0.500");
    period->setText("4.0");

    duty->setFixedWidth(90 * utils::ssf());
    period->setFixedWidth(90 * utils::ssf());

    duty->setVisible(false);
    period->setVisible(false);

    bottomLay->addWidget(duty);
    bottomLay->addWidget(period);

    connect(duty, &LineEditNum::userEditingFinished, this, [this] {
      m_setBtn->setVisible(true);
    });

    connect(period, &LineEditNum::userEditingFinished, this, [this] {
      m_setBtn->setVisible(true);
    });
  }

  bottomLay->addWidget(m_setBtn);
  bottomLay->addStretch();

  connect(m_slider, &QSlider::valueChanged, this, [this, type](int value) {
    if (type == ForceType::DUTY) {
      duty->setVisible(value == 3);
      period->setVisible(value == 3);
    }

    plcType = value;
    qDebug() << m_index;
  });

  connect(m_slider, &QSlider::sliderMoved, this, [this](int value) {
    switch (value) {
      case 0:
        m_valueLabel->setText(" ");
        break;
      case 3:
        m_valueLabel->setText("");
        break;
      default:
        m_valueLabel->setText(QString::number(value - 1));
        break;
    }
    m_valueLabel->setVisible(value != 3);

    m_setBtn->setVisible(true);
  });

  connect(m_setBtn, &QPushButton::clicked, this, [this] {
    m_setBtn->setVisible(false);
    emit generateRaw(1, getRawData());
  });

  connect(sysValue, &SystemValue::valueChanged, this, [indicator, index](float val) {
    indicator->setChecked(utils::getBitOfFloat(val, index));
  });

  setStyleSheet(utils::readStyleFromFile(":/styles/forceOutSlider.qss"));
}

QByteArray ForceOutSingleWidget::getRawData() const {
  QByteArray  result;
  QDataStream out(&result, QIODevice::WriteOnly);
  out.setByteOrder(QDataStream::LittleEndian);
  out.setFloatingPointPrecision(QDataStream::SinglePrecision);

  out << static_cast<uint8_t>(m_index);
  out << static_cast<uint8_t>(plcType);

  if (plcType == 3) {
    out << static_cast<float>(duty->text().toDouble());
    out << static_cast<float>(period->text().toDouble());
  }
  return result;
}
} // namespace control2
