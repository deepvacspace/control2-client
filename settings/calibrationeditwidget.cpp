#include "calibrationeditwidget.h"

#include <QGridLayout>
#include <QLabel>

#include "labelplus.h"
#include "utils.h"

namespace deepvaccontrol {
CalibrationEditWidget::CalibrationEditWidget(
  CalibrationModel *model, SystemValue *current, SystemValue *raw, QWidget *parent)
    : QWidget{parent}, m_model{model}, m_currentValue{current}, m_rawValue{raw} {
  m_valueSysValue     = new SystemValue{"", 0.0f, this};
  m_correctedSysValue = new SystemValue{"", 0.0f, this};
  m_deltaSysValue     = new SystemValue{"", 0.0f, this};

  auto *mainLay = new QGridLayout(this);

  mainLay->addWidget(new QLabel(tr("Текущее значение")), 0, 0);
  mainLay->addWidget(new LabelPlus(m_currentValue, this), 0, 1);

  mainLay->addWidget(new QLabel(tr("Значение без корректировки")), 1, 0);
  mainLay->addWidget(new LabelPlus(m_rawValue, this), 1, 1);

  m_valueSysValue->setMin(model->getMin());
  m_valueSysValue->setMax(model->getMax());
  m_valueSysValue->setPrec(1);

  m_correctedSysValue->setMin(m_valueSysValue->getValue() - model->getDeltaMax());
  m_correctedSysValue->setMax(m_valueSysValue->getValue() + model->getDeltaMax());
  m_correctedSysValue->setPrec(1);

  m_deltaSysValue->setMin(-model->getDeltaMax());
  m_deltaSysValue->setMax(model->getDeltaMax());
  m_deltaSysValue->setPrec(1);

  m_valueEdit     = new LineEditNum(m_valueSysValue);
  m_correctedEdit = new LineEditNum(m_correctedSysValue);
  m_deltaEdit     = new LineEditNum(m_deltaSysValue);
  m_deltaEdit->setEnabled(false);

  mainLay->addWidget(new QLabel(tr("Датчик камеры")), 2, 0);
  mainLay->addWidget(m_valueEdit, 2, 1);

  mainLay->addWidget(new QLabel(tr("Датчик внешнего прибора")), 3, 0);
  mainLay->addWidget(m_correctedEdit, 3, 1);

  mainLay->addWidget(new QLabel(tr("Корректировка")), 4, 0);
  mainLay->addWidget(m_deltaEdit, 4, 1);

  m_addBtn = new QPushButton(tr("Добавить точку"), this);
  m_delBtn = new QPushButton(tr("Удалить точку"), this);

  mainLay->addWidget(m_addBtn, 5, 0);
  mainLay->addWidget(m_delBtn, 5, 1);
  mainLay->setRowStretch(6, 1);

  for (auto &btn : {m_addBtn, m_delBtn}) {
    mainLay->setAlignment(btn, Qt::AlignHCenter);
    btn->setFixedSize(150 * utils::ssf(), 50 * utils::ssf());
  }

  connect(m_model, &CalibrationModel::indexSelected, this, &CalibrationEditWidget::updateGui);
  connect(m_model, &CalibrationModel::sizeChanged, this, &CalibrationEditWidget::handleSizeChanged);

  connect(m_addBtn, &QPushButton::clicked, this, [this]() {
    float const floatRound{std::round(m_rawValue->getValue() * 10) / 10};
    int const   addedPointIndex{m_model->addCalibPoint(floatRound, floatRound, 0.0f)};

    m_model->selectIndex(addedPointIndex, /*forse*/ true);
  });

  connect(m_delBtn, &QPushButton::clicked, this, [this]() {
    m_model->removePoint(m_model->getSelectedIndex());
  });

  connect(m_valueEdit, &LineEditNum::userEditingFinished, this, [this] {
    float const value{m_valueEdit->text().toFloat()};

    m_correctedEdit->setMin(value - m_model->getDeltaMax());
    m_correctedEdit->setMax(value + m_model->getDeltaMax());
    m_correctedSysValue->setMin(value - m_model->getDeltaMax());
    m_correctedSysValue->setMax(value + m_model->getDeltaMax());

    float const corrected{m_correctedEdit->text().toFloat()};
    float const delta{corrected - value};

    m_model->removePoint(m_model->getSelectedIndex());

    int const addedPointIndex{m_model->addCalibPoint(value, corrected, delta)};
    qDebug() << addedPointIndex;
    m_model->selectIndex(addedPointIndex, /*force*/ true);
  });

  connect(m_correctedEdit, &LineEditNum::userEditingFinished, this, [this] {
    float const value{m_valueEdit->text().toFloat()};
    float const corrected{m_correctedEdit->text().toFloat()};
    float const delta{corrected - value};

    m_deltaEdit->setValue(delta);

    m_model->item(m_model->getSelectedIndex(), 1)->setData(corrected, Qt::DisplayRole);
    m_model->item(m_model->getSelectedIndex(), 2)->setData(delta, Qt::DisplayRole);
  });

  handleSizeChanged(m_model->rowCount());
}

void CalibrationEditWidget::updateGui(int selectedIndex) {
  float value{0.0f};
  float corrected{0.0f};
  float delta{0.0f};

  bool const emptyModel{};
  if (-1 != selectedIndex) {
    auto *const valueItem     = m_model->item(selectedIndex, 0);
    auto *const correctedItem = m_model->item(selectedIndex, 1);
    auto *const deltaItem     = m_model->item(selectedIndex, 2);

    value     = valueItem->data(Qt::DisplayRole).toFloat();
    corrected = correctedItem->data(Qt::DisplayRole).toFloat();
    delta     = deltaItem->data(Qt::DisplayRole).toFloat();
  }

  m_valueSysValue->setValue(value);

  m_correctedSysValue->setMin(value - m_model->getDeltaMax());
  m_correctedSysValue->setMax(value + m_model->getDeltaMax());
  m_correctedEdit->setMin(m_correctedSysValue->getMin());
  m_correctedEdit->setMax(m_correctedSysValue->getMax());
  m_correctedSysValue->setValue(corrected);

  m_deltaSysValue->setValue(delta);
}

void CalibrationEditWidget::handleSizeChanged(int newSize) {
  m_valueEdit->setEnabled(0 != newSize);
  m_correctedEdit->setEnabled(0 != newSize);

  m_delBtn->setEnabled(0 != newSize);
  m_addBtn->setEnabled(newSize < m_model->getPoints());
}
} // namespace deepvaccontrol
