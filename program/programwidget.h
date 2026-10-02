#ifndef PROGRAMWIDGET_H
#define PROGRAMWIDGET_H

#include <QLabel>
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

  // program library (named, labelled programs shared with paired computers)
  void openLibrary();
  void saveToLibrary();
  void setLibraryProgram(qint64 id, QString const &name, QStringList const &labels);
  void updateProgramLabel();
  void updateRunStatus();

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
  QPushButton           *m_libraryBtn{nullptr};
  QPushButton           *m_saveLibraryBtn{nullptr};
  QLabel                *m_programLabel{nullptr}; // name of the library program being edited
  QLabel                *m_runStatus{nullptr};

  qint64      m_libraryId{-1};
  QString     m_libraryName;
  QStringList m_labels;
  bool        m_libraryModified{false};
  bool        m_loadingLibrary{false};
  QString     m_runningName; // program started from this page
  QStringList m_runningLabels;

  AccessManager::AccessLevel m_accessLevel;

  // ProgramModel *m_currentProgramModel;
};
} // namespace deepvaccontrol

#endif // PROGRAMWIDGET_H
