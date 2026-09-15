#ifndef PROGRAMWIDGET_H
#define PROGRAMWIDGET_H

#include <QWidget>

#include "cyclewidget.h"
#include "programgraph.h"
#include "programmodel.h"
#include "programtablewidget.h"
#include "resholder.h"

namespace deepvaccontrol {
class ProgramWidget : public QWidget {
  Q_OBJECT
public:
  explicit ProgramWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~ProgramWidget() = default;

private:
  void setProgramModel(ProgramModel *newProgramModel);
  void openIntervalEditDialog();
  void initGui();
  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);
  void updateGui();
  void sendProgram();

  ResHolder             *m_resHolder;
  ProgramModel          *m_model;
  ProgramTableWidget    *m_tableWidget;
  ProgramGraph          *m_programGraph;
  CycleWidget           *m_cycleWidget;
  QVector<QPushButton *> m_buttons;
  QPushButton           *m_startBtn;
  QPushButton           *m_startDelayedBtn;
  QPushButton           *m_saveBtn;
  QPushButton           *m_loadBtn;
  QPushButton           *m_newBtn;
  QPushButton           *m_editBtn;
  QPushButton           *m_deleteBtn;

  AccessManager::AccessLevel m_accessLevel;

  // ProgramModel *m_currentProgramModel;
};
} // namespace deepvaccontrol

#endif // PROGRAMWIDGET_H
