#ifndef INTERVALEDITDIALOG_H
#define INTERVALEDITDIALOG_H

#include <QCheckBox>
#include <QDataWidgetMapper>
#include <QDialog>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>

#include "programmodel.h"

namespace deepvaccontrol {

class IntervalHelpImageLabel : public QLabel {
  Q_OBJECT
public:
  explicit IntervalHelpImageLabel(QWidget *parent = nullptr) : QLabel(parent) {
  }
signals:
  void closeIt();

protected:
  void mousePressEvent(QMouseEvent *) override {
    emit closeIt();
  }
};

class IntervalHelpWindow : public QDialog {
  Q_OBJECT
public:
  explicit IntervalHelpWindow(QWidget *parent = nullptr);
};

class TempChangeSelector : public QGroupBox {
  Q_OBJECT
public:
  explicit TempChangeSelector(QString const &label, QWidget *parent = nullptr);
  bool isChecked() const {
    return setpoint->isChecked();
  }
  void setChecked(bool value) {
    setpoint->setChecked(value);
    ramp->setChecked(!value);
  }

private:
  QRadioButton *ramp;
  QRadioButton *setpoint;
};

class IntervalEditDialog : public QDialog {
  Q_OBJECT
public:
  explicit IntervalEditDialog(ProgramModel *model, QWidget *parent = nullptr);
  // QDialog interface
public slots:
  void reject() override;

signals:
  void selectedIndexChanged(int);

private:
  void initGui();
  void updateGuiElements(int newIndex);
  void localCommit();

  ProgramModel      *m_model;
  QStringList        paramsOrder;
  QDataWidgetMapper *mapper;

  QList<QWidget *>   paramWidgetList; // list of param for every interval (corresponding to JOSN model "scheduleStruct")
  QList<QCheckBox *> paramEnableWidgetList;
  QVector<QCheckBox *> userFlagsCheckBoxList;

  TempChangeSelector *maxSpeedCheck;

  double def_temp_min;
  double def_temp_max;

  QLabel                *intervalInfo;
  QPushButton           *nextButton;
  QPushButton           *prevButton;
  QPushButton           *newButton;
  QPushButton           *closeButton;
  QVector<QPushButton *> buttons;
};
} // namespace deepvaccontrol

#endif // INTERVALEDITDIALOG_H
