#ifndef PROGRAMWIDGET_H
#define PROGRAMWIDGET_H

#include <QLabel>
#include <QWidget>

#include "cyclewidget.h"
#include "programgraph.h"
#include "programmodel.h"
#include "intervallistview.h"
#include "resholder.h"

namespace deepvaccontrol {
class ProgramWidget : public QWidget {
  Q_OBJECT
public:
  explicit ProgramWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~ProgramWidget() = default;

protected:
  void changeEvent(QEvent *event) override;

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
  void updateSequenceInfo(); // interval count and selected position
  void updateButtonIcons();  // icons in the colours of the theme
  void labelsEdited();       // interval labels changed in the interval editor

  ResHolder             *m_resHolder;
  ProgramModel          *m_model;
  IntervalListView      *m_tableWidget;
  ProgramGraph          *m_programGraph;
  CycleWidget           *m_cycleWidget;
  QVector<QPushButton *> m_buttons;
  QPushButton           *m_startBtn;
  QPushButton           *m_startDelayedBtn;
  QPushButton           *m_newBtn;
  QPushButton           *m_editBtn;
  QPushButton           *m_deleteBtn;
  QPushButton           *m_libraryBtn{nullptr};     // "Load": a program from the program library (database)
  QPushButton           *m_saveLibraryBtn{nullptr}; // "Save": this program into the program library
  QLabel                *m_sequenceIcon{nullptr};
  QLabel                *m_countBadge{nullptr};     // number of intervals
  QLabel                *m_positionLabel{nullptr};  // "02 / 04": selected interval
  QLabel                *m_programLabel{nullptr}; // name of the library program being edited
  QLabel                *m_runStatus{nullptr};
  QLabel                *m_libraryBadge{nullptr};
  QLabel                *m_dutLabel{nullptr};

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
