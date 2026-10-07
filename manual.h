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

protected:
  void showEvent(QShowEvent *event) override;

private:
  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);
  void sendProgram();
  void initGui();
  void updateGui();
  // common tail of every start: AI advisor offer, send to PLC, event log, run recording
  bool     sendJob(ProgramModel *model, QString const &runLabel, QString const &testProfile, QString const &actionText);

  ResHolder *m_resHolder;

  // Live editable model
  SystemValueList *m_modelSysValuesList;
  ProgramModel    *m_model;

  QMap<int, QGroupBox *> m_stateWidgets;

  QPushButton *m_startBtn;
  QPushButton *m_startDelayedBtn;
  StopButton  *m_stopBtn;
  QWidget     *m_advisorNote{nullptr}; // shown while the AI Advisor offer is enabled (Settings -> AI)

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
    return !m_enableChkBox || m_enableChkBox->isChecked();
  }

private:
  SystemValueList *m_modelSysValuesList;
  SystemValue     *m_currentVal;
  QCheckBox       *m_enableChkBox{nullptr}; // only when the temperature can be switched off (chamber with pressure)
};

class CommonSetWidget : public QGroupBox {
  Q_OBJECT
public:
  // manualModel == nullptr: the quantity is not available on this chamber; the column is shown disabled
  explicit CommonSetWidget(quint32          index,
                           SystemValueList *manualModel,
                           SystemValue     *currentVal,
                           QString const   &title,
                           QString const   &unit,
                           QWidget         *parent = nullptr);

private:
  SystemValueList *m_modelSysValuesList;
  SystemValue     *m_currentVal;
};
} // namespace deepvaccontrol

#endif // MANUAL_H
