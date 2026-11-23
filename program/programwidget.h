#ifndef PROGRAMWIDGET_H
#define PROGRAMWIDGET_H

#include <QLabel>
#include <QTimer>
#include <QWidget>
#include <optional>

#include "cyclewidget.h"
#include "programforecast.h"
#include "programgraph.h"
#include "programmodel.h"
#include "resholder.h"

class QAbstractButton;
class QCheckBox;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QTabWidget;

namespace deepvaccontrol {
namespace ai {
class ProgramPlanRunner;
}
class IntervalCardList;
class LineEditNum;
class ManualGraph;
class ProgramStepper;
class RunDetailsForm;
class RunDetailsPanel;
class StopButton;

// Control page, in two tabs (the Analysis page's tab strip):
// - Manual (opened first): one card with the DUT (optional), the temperature (at full speed or a ramp rate) and / or
//   the pressure to hold until stopped (at least one), and Start / Delay start / Stop. Recorded as "Manual <date time>" by the logged-in user.
// - Program: on the left the experiment in three steps (1 experiment details, 2 intervals and cycle with the program
//   library, 3 confirmation with Start / Delay start / Stop); on the right the profile. While a program runs only step
//   3; while a setpoint runs (a program of one interval, sent as a setpoint job) every step, and Start replaces it once
//   confirmed.
class ProgramWidget : public QWidget {
  Q_OBJECT
public:
  explicit ProgramWidget(ResHolder *resHolder, QWidget *parent = nullptr);
  ~ProgramWidget() = default;
  void showManual();  // the Manual tab
  void showProgram(); // the Program tab

protected:
  void changeEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override; // the AI Advisor may have been switched on or off
  void resizeEvent(QResizeEvent *event) override; // the tabs and the card(s) a third of the width, the profile the rest

private:
  void setProgramModel(ProgramModel *newProgramModel);
  void openIntervalEditDialog();
  void initGui();
  void handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);
  void updateGui();
  // replace: a setpoint running is replaced (confirmed by the user); otherwise a job running stops the start
  void sendProgram(bool replace = false);
  // no program running (a setpoint may), the experiment details given and every interval with a setpoint; otherwise
  // says why (and opens the step, the interval) and returns false
  bool readyToStart();
  [[nodiscard]] bool setpointRunning() const; // a setpoint job runs: Start replaces it
  void               startNow();              // Start: once confirmed when it replaces a setpoint running
  [[nodiscard]] bool detailsComplete() const;        // step 1: name or purpose and operator
  [[nodiscard]] int  intervalWithoutSetpoint() const; // step 2: the first interval controlling nothing; -1: none
  bool               intervalsReady();                // step 2: when one controls nothing, says so and opens it

  // program library (named, labelled programs shared with paired computers)
  void openLibrary();
  void saveToLibrary();
  void setLibraryProgram(qint64 id, QString const &name, QStringList const &labels);
  void updateProgramLabel();
  void updateRunStatus();
  void updateSequenceInfo(); // total duration and when it would end
  void updateConfirmation(); // step 3's summary of the experiment, and the closed steps' summaries
  void updateButtonIcons();  // icons in the colours of the theme
  void labelsEdited();       // interval labels changed in the interval editor

  // how long the program takes, by the digital twin from the chamber's temperature (in the background)
  void programEdited();   // the estimate no longer fits the program
  void runForecast();     // after a short pause in the editing
  void temperatureMoved(); // a new estimate when the chamber's temperature has moved
  void showForecast(ai::ProgramForecast const &forecast);
  void updateForecastNotes();
  [[nodiscard]] bool forecastWithPlan(ai::ForecastInput const &input) const; // the advisor's estimate, not the user's

  // ---- the Manual tab
  void     buildManualPage(QWidget *&cardColumn, QWidget *&profile); // the card under the tabs, the profile right
  void     updateManualPreview(); // the profile and its summary: from the chamber's values to the setpoints, then held
  void     updateManualFields();  // enabled as switched on
  bool     buildManualProgram();  // what Start sends, the ramps from the chamber's values now, into m_manualModel
  [[nodiscard]] bool manualOn(QCheckBox const *box) const;        // switched on (and offered)
  [[nodiscard]] bool manualRamps(LineEditNum const *rate) const;  // its variable on and a ramp rate given
  void     manualToggled(QCheckBox *box); // at least one of temperature and pressure stays on
  bool     manualReady();                // no program running, the values given; otherwise says why
  void     manualStartNow();             // Start: once confirmed when it replaces a setpoint running
  void     sendManual(bool replace = false);
  void     updateManualButtons();

  ResHolder             *m_resHolder;
  ProgramModel          *m_model;
  ProgramStepper        *m_stepper{nullptr};
  RunDetailsPanel       *m_details{nullptr};
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
  // step 3: the experiment as it will start
  QLabel                *m_purposeValue{nullptr};
  QLabel                *m_dutValue{nullptr};
  QLabel                *m_operatorValue{nullptr};
  QLabel                *m_programValue{nullptr};
  QLabel                *m_cycleValue{nullptr};
  QLabel                *m_durationValue{nullptr};
  QLabel                *m_runStatus{nullptr};
  QLabel                *m_libraryBadge{nullptr};

  qint64      m_libraryId{-1};
  QString     m_libraryName;
  QStringList m_labels;
  bool        m_libraryModified{false};
  bool        m_loadingLibrary{false};
  QString     m_runningName; // program started from this page
  bool        m_working{false}; // a job ran at the last updateGui (step 3 is opened when one starts)
  QStringList m_runningLabels;

  AccessManager::AccessLevel m_accessLevel;

  ai::ProgramForecaster *m_forecaster{nullptr};
  ai::ProgramPlanRunner *m_planRunner{nullptr}; // the AI Advisor's settings, interval by interval
  QTimer                 m_forecastTimer;
  ai::ProgramForecast    m_forecast;
  bool                   m_forecastCurrent{false}; // m_forecast is for the program as it is now
  double                 m_forecastTemp{qQNaN()};  // the chamber's temperature it was asked for
  bool                   m_forecastPlanned{false}; // asked with the AI Advisor's settings

  // the tabs
  QTabBar        *m_modeTabs{nullptr};
  QWidget        *m_leftColumn{nullptr}; // the tabs over the open tab's card(s): a third of the width
  QStackedWidget *m_modeStack{nullptr};  // under the tabs: the Manual card, the Program steps
  QStackedWidget *m_graphStack{nullptr}; // right, the whole height: the open tab's profile
  // the Manual tab
  QWidget        *m_manualColumn{nullptr}; // its card, scrolled
  ProgramModel   *m_manualModel{nullptr};  // what Start sends: the ramps, if any, then the hold until stopped
  ManualGraph    *m_manualGraph{nullptr};
  RunDetailsForm *m_manualDut{nullptr};
  QCheckBox      *m_manualTempOn{nullptr};
  QCheckBox      *m_manualPresOn{nullptr};
  LineEditNum    *m_manualTemp{nullptr};
  LineEditNum    *m_manualPres{nullptr};
  LineEditNum    *m_manualRate{nullptr};     // °C/min; empty: full speed
  LineEditNum    *m_manualPresRate{nullptr}; // the chamber's unit per minute; empty: full speed
  QLabel         *m_manualSummary{nullptr};  // over the profile: "Ramp at 1.00 °C/min, then held until stopped"
  QString         m_manualPresUnit;          // the chamber's pressure unit
  QLabel         *m_manualStatus{nullptr};
  QPushButton    *m_manualStartBtn{nullptr};
  QPushButton    *m_manualDelayBtn{nullptr};
  StopButton     *m_manualStopBtn{nullptr};
};
}

#endif // PROGRAMWIDGET_H
