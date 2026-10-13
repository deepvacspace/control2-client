#ifndef HEAD_H
#define HEAD_H

#include <QVector>
#include <QLabel>
#include <QPushButton>
#include <QWidget>

#include "resholder.h"
#include "stopbutton.h"

namespace deepvaccontrol {
class ChamberView;
class ControlBar;

// Home page: live chamber sketch, chamber status and temperature, test time and program position.
class Head : public QWidget {
  Q_OBJECT
public:
  explicit Head(ResHolder *resHolder, QWidget *parent = nullptr);

  // Lazy setup all type of connection between components
  void setupGuiAndLogic();

protected:
  void changeEvent(QEvent *event) override;

private:
  void       handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);
  void       initBaseGui();
  void       restyle();
  void       updateGui();
  ResHolder *m_resHolder;

  ChamberView *m_chamber{nullptr};
  QLabel      *m_statusDot{nullptr};
  QLabel      *m_statusText{nullptr};
  QLabel      *m_tempValue{nullptr};
  QLabel      *m_tempUnit{nullptr};
  QVector<QWidget *> m_presWidgets; // caption and reading, shown when the chamber has a pressure gauge
  QLabel      *m_presValue{nullptr};
  QLabel      *m_presUnit{nullptr};
  QLabel      *m_coolingLabel{nullptr};
  QLabel      *m_heatingLabel{nullptr};
  QLabel      *m_setpointValue{nullptr};
  QLabel      *m_outputValue{nullptr};
  ControlBar  *m_outputBar{nullptr};
  QLabel      *m_curWorkTimeLabel{nullptr};
  QLabel      *m_intervalLabel{nullptr};
  QLabel      *m_cycleLabel{nullptr};
  QWidget     *m_intervalBox{nullptr};
  QWidget     *m_cycleBox{nullptr};

  StopButton  *m_stopBtn;
  QPushButton *m_nextIntervalBtn;

  AccessManager::AccessLevel m_accessLevel;
};
} // namespace deepvaccontrol

#endif // HEAD_H
