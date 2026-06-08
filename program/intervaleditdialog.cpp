#include "intervaleditdialog.h"

#include <QHBoxLayout>

#include "constants.h"
#include "intervaleditdelegate.h"
#include "lineeditnum.h"
#include "lineedittime.h"
#include "utils.h"

namespace control2 {
IntervalEditDialog::IntervalEditDialog(ProgramModel *model, QWidget *parent) : QDialog{parent}, m_model{model} {
  paramsOrder = m_model->getParamListOrder();

  initGui();

  mapper = new QDataWidgetMapper(this);
  mapper->setModel(m_model->getStandardModel());
  mapper->setItemDelegate(new IntervalEditDelegate(this));
  mapper->setOrientation(Qt::Vertical);
  mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);

  for (int i{0U}; i < paramWidgetList.size(); ++i) {
    auto &item = paramWidgetList[i];
    mapper->addMapping(item, i);
  }

  connect(mapper, &QDataWidgetMapper::currentIndexChanged, this, [this](int index) {
    updateGuiElements(index);
    m_model->selectInterval(index);
  });

  connect(nextButton, &QPushButton::clicked, this, [this] {
    localCommit();
    mapper->toNext();
  });

  connect(prevButton, &QPushButton::clicked, this, [this] {
    localCommit();
    mapper->toPrevious();
  });

  connect(closeButton, &QPushButton::clicked, this, [this] {
    localCommit();
    close();
  });

  connect(newButton, &QPushButton::clicked, this, [this] {
    localCommit();
    m_model->newItem(mapper->currentIndex() + 1);
    mapper->toNext();
  });

  mapper->setCurrentIndex(m_model->getSelectedInterval());
  updateGuiElements(mapper->currentIndex());

  setAttribute(Qt::WA_DeleteOnClose);
}

void IntervalEditDialog::initGui() {
  intervalInfo = new QLabel(this);

  nextButton  = new QPushButton(tr("Следующий"), this);
  prevButton  = new QPushButton(tr("Предыдущий"), this);
  newButton   = new QPushButton(tr("Добавить новый"), this);
  closeButton = new QPushButton(tr("Закрыть"), this);
  buttons << nextButton << prevButton << newButton << closeButton;

  for (QPushButton *btn : buttons) {
    btn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());
  }

  maxSpeedCheck = new TempChangeSelector(tr("Изменение температуры"), this);
  maxSpeedCheck->setMaximumWidth(300 * utils::ssf());

  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(10 * utils::ssf(), 10 * utils::ssf(), 10 * utils::ssf(), 10 * utils::ssf());
  auto *bottomLay = new QHBoxLayout();
  auto *paramLay  = new QGridLayout();

  // cycle all params in Interval Model Struct (duration, temp, hum, etc.)
  int indexPositionInLay = 0; // != index in model->getModelStruct(), so count separately
  for (auto const &item : m_model->getParamListVariant()) {
    int const     type{item.toMap()["type"].toInt()};
    QString const label{item.toMap()["label"].toString()};
    QString const name{item.toMap()["name"].toString()};

    //        if(name=="flags")   // FIXME ? is it right solution to ignore 'flags' parameter in layout? Does it couse
    //        any other problem in design?
    //            continue;

    // param's edit widget
    switch (type) {
      case 0: // regular double precision number edit line
      {
        double const  min{item.toMap()["min"].toDouble()};
        double const  max{item.toMap()["max"].toDouble()};
        double const  prec{item.toMap()["prec"].toDouble()};
        auto         *numEditWidget = new LineEditNum(min, max, prec, true, this);
        QString const notation{item.toMap()["notation"].toString()};
        numEditWidget->setIsScientific(notation == "scientific");
        paramWidgetList << numEditWidget;

        if ("temp" == name) {
          def_temp_min = min;
          def_temp_max = max;
        }
        break;
      }
      case static_cast<int>(Qt::UserRole) + 0: {
        paramWidgetList << new LineEditTime(true, this);
        break;
      }
      // case (int)Qt::UserRole + 2: {
      //   auto ls = new LampSlider(this);
      //   ls->setMinimumWidth(200);
      //   ls->setEnabled(true);
      //   paramWidgetList << ls;
      //   break;
      // }
      case static_cast<int>(Qt::UserRole) + 3: {
        auto ew = new QWidget(this);
        ew->setVisible(false);
        paramWidgetList << ew;
        break;
      }
      default: {
        auto *ew = new QWidget(this);
        ew->setVisible(false);
        paramWidgetList << ew;
        break;
      }
    }
    //

    // param's enable checkbox
    auto *enableCheckBox = new QCheckBox(this);
    if (QStringList{"duration", "flags", "lum"}.contains(name)) {
      enableCheckBox->setVisible(false);
    }
    paramEnableWidgetList << enableCheckBox;
    auto *paramWidgetLast = paramWidgetList.last();
    connect(enableCheckBox, &QCheckBox::toggled, this, [paramWidgetLast](bool value) {
      paramWidgetLast->setEnabled(value);
    });

    // param's name label widget
    auto *nameWidget = new QLabel(label, this);
    if (QStringList{"flags", "lum"}.contains(name)) {
      nameWidget->setVisible(false);
    }

    // place elemaent for each parameter
    paramLay->addWidget(nameWidget, indexPositionInLay, 0);
    paramLay->addWidget(paramEnableWidgetList.last(), indexPositionInLay, 1);
    paramLay->addWidget(paramWidgetList.last(), indexPositionInLay, 2);

    paramLay->setAlignment(nameWidget, Qt::AlignVCenter);

    ++indexPositionInLay;
  }
  //

  // // NOTE Place for user interval flags
  // for (auto const &item : userFlagsMap) {
  //   auto label = item.toMap().value("label").toString();
  //   auto name  = item.toMap().value("name").toString();
  //   auto cb    = new QCheckBox(this);
  //   cb->setChecked(false);
  //   userFlagsCheckBoxList << cb;
  //   paramLay->addWidget(new QLabel(label, this), indexPositionInLay, 0);
  //   paramLay->addWidget(cb, indexPositionInLay, 1);
  //   ++indexPositionInLay;

  //   if (name == "nitrogen" && paramsOrder.contains("temp")) {
  //     connect(cb, &QCheckBox::toggled, this, [&](bool value) {
  //       auto tempSetWidget = qobject_cast<LineEditNum *>(paramWidgetList[paramsOrder.indexOf("temp")]);
  //       tempSetWidget->setMinMaxPrec(value ? -200 : def_temp_min, value ? -40 : def_temp_max,
  //       tempSetWidget->getPrec());
  //     });
  //   }
  // }
  // //

  // STATE WIDGETS INTERACTION
  if (paramsOrder.contains("temp")) {
    connect(paramEnableWidgetList.at(paramsOrder.indexOf("temp")), &QCheckBox::toggled, this, [&](bool value) {
      if (value) {
        if (paramsOrder.contains("pres")) {
          paramEnableWidgetList.at(paramsOrder.indexOf("pres"))->setChecked(false);
        }
      } else {
        if (paramsOrder.contains("hum")) {
          paramEnableWidgetList.at(paramsOrder.indexOf("hum"))->setChecked(false);
        }
      }

      maxSpeedCheck->setEnabled(value);
    });
  }
  if (paramsOrder.contains("hum")) {
    connect(paramEnableWidgetList.at(paramsOrder.indexOf("hum")), &QCheckBox::toggled, this, [&](bool value) {
      if (value) {
        if (paramsOrder.contains("temp")) {
          paramEnableWidgetList.at(paramsOrder.indexOf("temp"))->setChecked(true);
        }
        if (paramsOrder.contains("pres")) {
          paramEnableWidgetList.at(paramsOrder.indexOf("pres"))->setChecked(false);
        }
      }

      auto tempSetWidget = qobject_cast<LineEditNum *>(paramWidgetList[paramsOrder.indexOf("temp")]);
      tempSetWidget->setMinMaxPrec(value ? 10 : def_temp_min, value ? 85 : def_temp_max, tempSetWidget->getPrec());
    });
  }
  if (paramsOrder.contains("pres")) {
    connect(paramEnableWidgetList.at(paramsOrder.indexOf("pres")), &QCheckBox::toggled, this, [&](bool value) {
      if (value) {
        if (paramsOrder.contains("temp")) {
          paramEnableWidgetList.at(paramsOrder.indexOf("temp"))->setChecked(false);
        }
        if (paramsOrder.contains("hum")) {
          paramEnableWidgetList.at(paramsOrder.indexOf("hum"))->setChecked(false);
        }
      }
    });
  }
  //

  paramLay->addWidget(maxSpeedCheck, indexPositionInLay, 0, 1, 3);
  paramLay->setRowStretch(indexPositionInLay + 1, 1);

  auto *buttonLay = new QVBoxLayout();
  buttonLay->addWidget(prevButton);
  buttonLay->addWidget(nextButton);
  buttonLay->addWidget(newButton);
  buttonLay->addStretch();
  buttonLay->addWidget(closeButton);

  bottomLay->addLayout(buttonLay);
  bottomLay->addLayout(paramLay);

  mainLay->addWidget(intervalInfo);
  mainLay->addLayout(bottomLay);

  mainLay->setAlignment(intervalInfo, Qt::AlignHCenter);

  setStyleSheet(utils::readStyleFromFile(":/styles/checkBoxBig.qss")
                + QString("QCheckBox::indicator{width: %1px; height: %1px;}").arg(26 * utils::ssf()));

  //    mainLay->setSizeConstraint(QLayout::SetFixedSize);
  this->adjustSize(); // NOTE to avoid "setGeometry: Unable to set geometry"
  this->setWindowTitle("Редактирование интервала");
  this->setMinimumWidth(400);
}

void IntervalEditDialog::updateGuiElements(int newIndex) {
  quint32 flags{m_model->getStandardModel()->item(m_model->getFlagsIndex(), newIndex)->data(Qt::UserRole + 1).toUInt()};

  for (int i{0U}; i < paramWidgetList.size(); ++i) {
    bool en = ((flags >> i) & 0x01);
    paramEnableWidgetList[i]->setChecked(en);
    paramWidgetList[i]->setEnabled(en);
  }

  // NOTE Section for userFlagsList updates
  // for (int i = 0; i < userFlagsMap.keys().size(); ++i) {
  //   auto bit = userFlagsMap.keys().at(i);
  //   userFlagsCheckBoxList[i]->setChecked(flags >> bit & 0x01);
  // }

  maxSpeedCheck->setChecked(utils::getBitOfDword(flags, control2::AWAIT_BIT));

  int const count{m_model->getStandardModel()->columnCount()};

  prevButton->setEnabled(newIndex > 0);
  nextButton->setEnabled(newIndex < (count - 1));
  intervalInfo->setText(QString(tr("Интервал %1/%2")).arg(newIndex + 1).arg(count));
}

void IntervalEditDialog::localCommit() {
  int const curIndex{mapper->currentIndex()};
  quint32 flags{m_model->getStandardModel()->item(m_model->getFlagsIndex(), curIndex)->data(Qt::UserRole + 1).toUInt()};

  for (int i{0U}; i < paramWidgetList.size(); ++i) {
    bool const isEnabled{paramWidgetList[i]->isEnabled()};
    utils::setBitOfDword(flags, i, isEnabled);
  }

  // for (int i{0U}; i < userFlagsMap.keys().size(); ++i) {
  //   auto bit = userFlagsMap.keys().at(i);
  //   flags    = (flags & ~(0x01 << bit)) | (userFlagsCheckBoxList[i]->isChecked() << bit);
  // }

  bool const tempIsEnabled{utils::getBitOfDword(flags, paramsOrder.indexOf("temp"))};
  utils::setBitOfDword(flags, control2::AWAIT_BIT, tempIsEnabled & maxSpeedCheck->isChecked());
  utils::setBitOfDword(flags, control2::RAMP_BIT, tempIsEnabled & (!maxSpeedCheck->isChecked()));

  // flags does not mapped to anything, so 'commit' it value manualy
  m_model->getStandardModel()->item(m_model->getFlagsIndex(), curIndex)->setData(flags, Qt::UserRole + 1);
  mapper->submit();
  m_model->emitModelUpdated();
}

void IntervalEditDialog::reject() {
  localCommit();
  emit selectedIndexChanged(mapper->currentIndex());
  QDialog::reject();
}

TempChangeSelector::TempChangeSelector(QString const &label, QWidget *parent) : QGroupBox{label, parent} {
  auto bbLay0 = new QHBoxLayout(this);
  auto bbLay  = new QVBoxLayout();
  bbLay0->addLayout(bbLay);
  auto bbBtn = new QPushButton();
  bbBtn->setIcon(QIcon(":/imgs/question_mark.png"));
  bbBtn->setIconSize(QSize(15 * utils::ssf(), 15 * utils::ssf()));
  bbBtn->setFixedSize(25 * utils::ssf(), 25 * utils::ssf());

  ramp     = new QRadioButton(tr("линеное"), this);
  setpoint = new QRadioButton(tr("с максимальной скоростью"), this);

  bbLay->addWidget(setpoint);
  bbLay->addWidget(ramp);
  bbLay0->addWidget(bbBtn);
  bbLay0->setAlignment(bbBtn, Qt::AlignBottom);
  setStyleSheet(QString("QRadioButton::indicator{width: %1px; height: %1px;}").arg(20 * utils::ssf())
                + utils::readStyleFromFile(":/styles/stateVisBox.qss")
                    .arg(4 * utils::ssf())
                    .arg(8.5 * utils::ssf())
                    .arg(0 * utils::ssf())
                    .arg(25 * utils::ssf())
                    .arg(26 * utils::ssf()));
  setpoint->setChecked(true);
  connect(bbBtn, &QPushButton::clicked, this, [this] {
    auto *aa = new IntervalHelpWindow(this);
    aa->open();
  });
}

IntervalHelpWindow::IntervalHelpWindow(QWidget *parent) : QDialog(parent) {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);

  auto   *pixLabel = new IntervalHelpImageLabel(this);
  QPixmap usbPix(QPixmap(":/imgs/temp_type.png").scaledToWidth(600 * utils::ssf(), Qt::SmoothTransformation));
  pixLabel->setPixmap(usbPix);
  lay->addWidget(pixLabel);

  connect(pixLabel, &IntervalHelpImageLabel::closeIt, this, &QDialog::accept);
  setWindowTitle(tr("Изменение скорости"));
  setAttribute(Qt::WA_DeleteOnClose);
}
} // namespace control2
