#ifndef QUICKSETPOINT_H
#define QUICKSETPOINT_H

#include <QWidget>

#include "resholder.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace deepvaccontrol {

class LineEditNum;
class LineEditTime;
class ProgramModel;
class StopButton;

/**
 * @brief Program page, Quick setpoint tab (formerly the Manual control page): temperature or pressure to a setpoint,
 * optionally ramped at a rate (temperature) and held for a time; without a hold time it is held until stopped.
 *
 * Start, Delay start and Stop of its own. Start while a quick setpoint runs replaces it (no stop between); while a
 * program runs it must be stopped first.
 */
class QuickSetpointPanel : public QWidget {
  Q_OBJECT
public:
  explicit QuickSetpointPanel(ResHolder *resHolder, QWidget *parent = nullptr);

private:
  void updateGui();    // the fields of the variable, the buttons of the chamber's status and the user's level
  void start();        // checks, then sends the job
  bool buildJob();     // m_model from the fields; false: says why
  [[nodiscard]] QString pressureUnit() const;

  ResHolder     *m_resHolder;
  ProgramModel  *m_model{nullptr}; // the job sent (kept for the AI Advisor's offer)
  int            m_tempRow{-1};
  int            m_presRow{-1};
  QComboBox     *m_variableCombo{nullptr};
  QLabel        *m_setpointLabel{nullptr};
  LineEditNum   *m_tempEdit{nullptr};
  LineEditNum   *m_presEdit{nullptr};
  QCheckBox     *m_rampCheck{nullptr};
  LineEditNum   *m_rateEdit{nullptr};
  QCheckBox     *m_holdCheck{nullptr};
  LineEditTime  *m_holdEdit{nullptr};
  QPushButton   *m_startBtn{nullptr};
  QPushButton   *m_startDelayedBtn{nullptr};
  StopButton    *m_stopBtn{nullptr};
};

}

#endif // QUICKSETPOINT_H
