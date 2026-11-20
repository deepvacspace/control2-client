#include "calibrationeditwidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

#include "labelplus.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
CalibrationEditWidget::CalibrationEditWidget(
  CalibrationModel *model, SystemValue *current, SystemValue *raw, QWidget *parent)
    : QWidget{parent}, m_model{model}, m_currentValue{current}, m_rawValue{raw} {
  m_valueSysValue     = new SystemValue{"", 0.0f, this};
  m_correctedSysValue = new SystemValue{"", 0.0f, this};
  m_deltaSysValue     = new SystemValue{"", 0.0f, this};

  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(theme::dp(12));

  auto muted = [this](QString const &text) {
    auto *label = new QLabel(text, this);
    label->setProperty("muted", true);
    return label;
  };
  // live readings side by side
  auto *readings = new QGridLayout();
  readings->setHorizontalSpacing(theme::dp(44));
  readings->setVerticalSpacing(theme::dp(4));
  QFont bigFont = font();
  bigFont.setBold(true);
  bigFont.setPointSizeF(font().pointSizeF() * 1.5);
  auto *currentLabel = new LabelPlus(m_currentValue, this);
  auto *rawLabel     = new LabelPlus(m_rawValue, this);
  currentLabel->setFont(bigFont);
  rawLabel->setFont(bigFont);
  readings->addWidget(muted(tr("Current value")), 0, 0);
  readings->addWidget(muted(tr("Uncalibrated value")), 0, 1);
  readings->addWidget(currentLabel, 1, 0);
  readings->addWidget(rawLabel, 1, 1);
  readings->setColumnStretch(2, 1);
  mainLay->addLayout(readings);
  auto *divider = new QFrame(this);
  divider->setObjectName("cardDivider");
  divider->setFixedHeight(qMax(1, theme::dp(1)));
  mainLay->addSpacing(theme::dp(12));
  mainLay->addWidget(divider);
  mainLay->addSpacing(theme::dp(12));

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

  // the selected point
  auto *fields = new QGridLayout();
  fields->setHorizontalSpacing(theme::dp(24));
  fields->setVerticalSpacing(theme::dp(16));
  int row{0};
  for (auto const &[title, edit] : {std::pair{tr("Chamber sensor"), m_valueEdit}, std::pair{tr("External reference"), m_correctedEdit},
                                    std::pair{tr("Correction"), m_deltaEdit}}) {
    auto *label = new QLabel(title, this);
    label->setObjectName("rowTitle");
    edit->setFont(font());
    edit->setFixedSize(theme::dp(281), theme::dp(56));
    fields->addWidget(label, row, 0);
    fields->addWidget(edit, row, 1);
    ++row;
  }
  fields->setColumnStretch(2, 1);
  mainLay->addLayout(fields);
  mainLay->addSpacing(theme::dp(20));

  m_addBtn = new QPushButton(tr("Add point"), this);
  m_addBtn->setObjectName("primaryButton");
  m_delBtn = new QPushButton(tr("Delete point"), this);
  m_delBtn->setProperty("outline", true);
  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(theme::dp(16));
  for (auto &btn : {m_addBtn, m_delBtn}) {
    btn->setMinimumSize(theme::dp(243), theme::dp(56));
    buttons->addWidget(btn);
  }
  buttons->addStretch();
  mainLay->addLayout(buttons);

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
}
