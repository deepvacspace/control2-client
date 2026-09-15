#ifndef MANUAL_H
#define MANUAL_H

#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QWidget>

#include "programmodel.h"
#include "resholder.h"
#include "stopbutton.h"

namespace deepvaccontrol {
class Manual : public QWidget {
  Q_OBJECT
public:
  explicit Manual(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);
  void sendProgram();
  void initGui();
  void updateGui();

  ResHolder *m_resHolder;

  // Live editable model
  SystemValueList *m_modelSysValuesList;
  ProgramModel    *m_model;

  QMap<int, QGroupBox *> m_stateWidgets;

  QPushButton *m_startBtn;
  QPushButton *m_startDelayedBtn;
  StopButton  *m_stopBtn;

  AccessManager::AccessLevel m_accessLevel;

  SystemValue *m_modelFlags; // in m_modelSysValuesList
};

class TempSetWidget : public QGroupBox {
  Q_OBJECT
public:
  explicit TempSetWidget(quint32          index,
                         SystemValueList *manualModel,
                         SystemValue     *currentVal,
                         bool             speedUnitIsHour = false,
                         bool             alwaysEnable    = true,
                         QWidget         *parent          = nullptr);

  bool isEnabled() const {
    return m_enableChkBox->isChecked();
  }

private:
  SystemValueList *m_modelSysValuesList;
  SystemValue     *m_currentVal;
  QCheckBox       *m_enableChkBox;
};

class CommonSetWidget : public QGroupBox {
  Q_OBJECT
public:
  explicit CommonSetWidget(
    quint32 index, SystemValueList *manualModel, SystemValue *currentVal, QWidget *parent = nullptr);

private:
  SystemValueList *m_modelSysValuesList;
  SystemValue     *m_currentVal;
};
} // namespace deepvaccontrol

#endif // MANUAL_H
