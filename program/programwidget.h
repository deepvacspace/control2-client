#ifndef PROGRAMWIDGET_H
#define PROGRAMWIDGET_H

#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "cyclewidget.h"
#include "programforecast.h"
#include "programgraph.h"
#include "programmodel.h"
#include "resholder.h"

class QAbstractButton;
class QPushButton;

namespace deepvaccontrol {
namespace ai {
class ProgramPlanRunner;
}
class ChartSideBar;
class IntervalCardList;
class StopButton;

// Program page: the program's profile, and below it Start / Delay start / Stop; beside it a drawer like the chart's
// (Intervals with the cycle under them, Experiment details; Save and Load at the bottom of its rail).
class ProgramWidget : public QWidget {
  Q_OBJECT
public:
  explicit ProgramWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~ProgramWidget() = default;

protected:
  void changeEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override; // the Intervals drawer opens; the AI Advisor may have been switched on or off

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
  void updateSequenceInfo(); // total duration and when it would end
  void updateBottomBar();    // cycle and device under test
  void updateButtonIcons();  // icons in the colours of the theme
  void labelsEdited();       // interval labels changed in the interval editor

  // how long the program takes, by the digital twin from the chamber's temperature (in the background)
  void programEdited();   // the estimate no longer fits the program
  void runForecast();     // after a short pause in the editing
  void temperatureMoved(); // a new estimate when the chamber's temperature has moved
  void showForecast(ai::ProgramForecast const &forecast);
  void updateForecastNotes();
  [[nodiscard]] bool forecastWithPlan(ai::ForecastInput const &input) const; // the advisor's estimate, not the user's

  ResHolder             *m_resHolder;
  ProgramModel          *m_model;
  ChartSideBar          *m_drawer{nullptr};
  IntervalCardList      *m_intervalList{nullptr};
  ProgramGraph          *m_programGraph;
  CycleWidget           *m_cycleWidget;
  QPushButton           *m_startBtn;
  QPushButton           *m_startDelayedBtn;
  StopButton            *m_stopBtn{nullptr};
  QPushButton           *m_newBtn;                   // "Add": an interval after the selected one
  QAbstractButton       *m_libraryBtn{nullptr};     // "Load": a program from the program library (database)
  QAbstractButton       *m_saveLibraryBtn{nullptr}; // "Save": this program into the program library
  QLabel                *m_programLabel{nullptr}; // name of the library program being edited ("Untitled program")
  QLabel                *m_summaryLabel{nullptr}; // "6 h 00 min · ends 19:50 if started now"
  QTimer                 m_summaryTimer;          // the end time follows the clock
  QLabel                *m_cycleValue{nullptr};   // in the bar below the profile
  QLabel                *m_dutValue{nullptr};
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

  ai::ProgramForecaster *m_forecaster{nullptr};
  ai::ProgramPlanRunner *m_planRunner{nullptr}; // the AI Advisor's settings, interval by interval
  QTimer                 m_forecastTimer;
  ai::ProgramForecast    m_forecast;
  bool                   m_forecastCurrent{false}; // m_forecast is for the program as it is now
  double                 m_forecastTemp{qQNaN()};  // the chamber's temperature it was asked for
  bool                   m_forecastPlanned{false}; // asked with the AI Advisor's settings

  // ProgramModel *m_currentProgramModel;
};
}

#endif // PROGRAMWIDGET_H
