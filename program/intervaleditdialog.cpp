#include "intervaleditdialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QButtonGroup>

#include "constants.h"
#include "intervaleditdelegate.h"
#include "lineeditnum.h"
#include "lineeditscreen.h"
#include "lineedittime.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
IntervalEditDialog::IntervalEditDialog(ProgramModel *model, QStringList *labels, QWidget *parent)
    : QDialog{parent}, m_model{model}, m_labels{labels} {
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
    if (m_labels) {
      while (m_labels->size() < m_model->getStandardModel()->columnCount()) {
        m_labels->append(QString());
      }
      m_labels->insert(mapper->currentIndex() + 1, QString());
      emit labelsChanged();
    }
    m_model->newItem(mapper->currentIndex() + 1);
    mapper->toNext();
  });

  mapper->setCurrentIndex(m_model->getSelectedInterval());
  updateGuiElements(mapper->currentIndex());

  setAttribute(Qt::WA_DeleteOnClose);
}

void IntervalEditDialog::initGui() {
  intervalInfo = new QLabel(this);

  double const ssf{utils::ssf()};
  QFont        titleFont = intervalInfo->font();
  titleFont.setBold(true);
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.1);
  intervalInfo->setFont(titleFont);

  // ‹ previous · next ›, add after, close
  auto navButton = [this, ssf](QString const &kind, QString const &tip) {
    auto *button = new QPushButton(this);
    theme::setThemedIcon(button, kind, static_cast<int>(16 * ssf));
    button->setFixedSize(static_cast<int>(34 * ssf), static_cast<int>(34 * ssf));
    button->setToolTip(tip);
    return button;
  };
  prevButton  = navButton("left", tr("Previous interval"));
  nextButton  = navButton("right", tr("Next interval"));
  newButton   = new QPushButton(tr("Add interval after"), this);
  closeButton = new QPushButton(tr("Close"), this);
  closeButton->setObjectName("primaryButton");
  for (QPushButton *btn : {newButton, closeButton}) {
    btn->setFixedHeight(static_cast<int>(34 * ssf));
    btn->setMinimumWidth(static_cast<int>(130 * ssf));
  }
  buttons << nextButton << prevButton << newButton << closeButton;

  maxSpeedCheck = new TempChangeSelector(QString(), this);

  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(18 * ssf, 14 * ssf, 18 * ssf, 14 * ssf);
  mainLay->setSpacing(12 * ssf);
  auto *bottomLay = new QHBoxLayout();
  auto *paramLay  = new QGridLayout();
  paramLay->setHorizontalSpacing(10 * ssf);
  paramLay->setVerticalSpacing(8 * ssf);

  // cycle all params in Interval Model Struct (duration, temp, hum, etc.)
  // interval label (shown in the interval table, saved with the program)
  labelEdit = new LineEditScreen(this, VirtualKeyboard::EN);
  labelEdit->setPlaceholderText(tr("e.g. Ramp up, Hot dwell"));
  labelEdit->setMinimumHeight(32 * utils::ssf());
  labelEdit->setEnabled(m_labels != nullptr);
  auto *labelTitle = new QLabel(tr("Label"), this);
  labelTitle->setProperty("muted", true);
  labelEdit->setFixedHeight(static_cast<int>(32 * utils::ssf()));
  paramLay->addWidget(labelTitle, 0, 0);
  paramLay->addWidget(labelEdit, 0, 2);
  int indexPositionInLay = 1; // != index in model->getModelStruct(), so count separately
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
        numEditWidget->setFont(font());
        numEditWidget->setFixedSize(static_cast<int>(160 * utils::ssf()), static_cast<int>(32 * utils::ssf()));
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
        auto *timeEdit = new LineEditTime(true, this);
        timeEdit->setFont(font());
        timeEdit->setFixedSize(static_cast<int>(160 * utils::ssf()), static_cast<int>(32 * utils::ssf()));
        for (LineEditNum *part : timeEdit->findChildren<LineEditNum *>()) {
          part->setFont(font());
          part->setFixedHeight(static_cast<int>(32 * utils::ssf()));
        }
        paramWidgetList << timeEdit;
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
    nameWidget->setProperty("muted", true);
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

  auto *modeTitle = new QLabel(tr("Temperature change"), this);
  modeTitle->setProperty("muted", true);
  paramLay->addWidget(modeTitle, indexPositionInLay, 0);
  paramLay->addWidget(maxSpeedCheck, indexPositionInLay, 2);
  paramLay->setColumnStretch(3, 1);

  auto *header = new QHBoxLayout();
  header->setSpacing(10 * ssf);
  header->addWidget(prevButton);
  header->addWidget(intervalInfo);
  header->addWidget(nextButton);
  header->addStretch();
  mainLay->addLayout(header);
  auto *divider = new QFrame(this);
  divider->setObjectName("cardDivider");
  divider->setFixedHeight(qMax(1, static_cast<int>(1 * ssf)));
  mainLay->addWidget(divider);
  mainLay->addLayout(paramLay);
  mainLay->addStretch();
  bottomLay->addWidget(newButton);
  bottomLay->addStretch();
  bottomLay->addWidget(closeButton);
  mainLay->addLayout(bottomLay);

  //    mainLay->setSizeConstraint(QLayout::SetFixedSize);
  this->adjustSize(); // NOTE to avoid "setGeometry: Unable to set geometry"
  this->setWindowTitle(tr("Edit interval"));
  this->setMinimumWidth(static_cast<int>(560 * utils::ssf()));
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

  maxSpeedCheck->setChecked(utils::getBitOfDword(flags, deepvaccontrol::AWAIT_BIT));
  labelEdit->setText(m_labels ? m_labels->value(newIndex) : QString());

  int const count{m_model->getStandardModel()->columnCount()};

  prevButton->setEnabled(newIndex > 0);
  nextButton->setEnabled(newIndex < (count - 1));
  intervalInfo->setText(tr("Interval %1 of %2").arg(newIndex + 1).arg(count));
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
  utils::setBitOfDword(flags, deepvaccontrol::AWAIT_BIT, tempIsEnabled & maxSpeedCheck->isChecked());
  utils::setBitOfDword(flags, deepvaccontrol::RAMP_BIT, tempIsEnabled & (!maxSpeedCheck->isChecked()));

  if (m_labels) {
    while (m_labels->size() < m_model->getStandardModel()->columnCount()) {
      m_labels->append(QString());
    }
    QString const label = labelEdit->text().trimmed();
    if ((curIndex >= 0) && (curIndex < m_labels->size()) && ((*m_labels)[curIndex] != label)) {
      (*m_labels)[curIndex] = label;
      emit labelsChanged();
    }
  }

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

TempChangeSelector::TempChangeSelector(QString const &label, QWidget *parent) : QWidget{parent} {
  Q_UNUSED(label)
  double const ssf{utils::ssf()};
  auto        *lay = new QHBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(0);
  setpoint = new QPushButton(tr("At maximum rate"), this);
  ramp     = new QPushButton(tr("Linear ramp"), this);
  setpoint->setToolTip(tr("Full speed to the target, then hold it"));
  ramp->setToolTip(tr("Ramp linearly over the interval"));
  auto *group = new QButtonGroup(this);
  int   index{0};
  for (QPushButton *button : {setpoint, ramp}) {
    button->setCheckable(true);
    button->setProperty("segment", true);
    button->setProperty("segmentFirst", index == 0);
    button->setProperty("segmentLast", index == 1);
    button->setFixedHeight(static_cast<int>(32 * ssf));
    button->setMinimumWidth(static_cast<int>(140 * ssf));
    group->addButton(button, index++);
    lay->addWidget(button);
  }
  group->setExclusive(true);
  lay->addSpacing(8 * ssf);
  auto *bbBtn = new QPushButton(QStringLiteral("?"), this);
  bbBtn->setFixedSize(static_cast<int>(32 * ssf), static_cast<int>(32 * ssf));
  bbBtn->setToolTip(tr("How the two modes differ"));
  lay->addWidget(bbBtn);
  lay->addStretch();
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
  setWindowTitle(tr("Rate change"));
  setAttribute(Qt::WA_DeleteOnClose);
}
}
