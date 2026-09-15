#ifndef HEAD_H
#define HEAD_H

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "resholder.h"
#include "stopbutton.h"

namespace deepvaccontrol {
class Head : public QWidget {
  Q_OBJECT
public:
  explicit Head(ResHolder *resHolder, QWidget *parent = nullptr);

  // Lazy setup all type of connection between components
  void setupGuiAndLogic();

private:
  void       handleAccessChange(QString const &name, AccessManager::AccessLevel newLevel);
  void       initBaseGui();
  void       updateGui();
  ResHolder *m_resHolder;

  QVBoxLayout *m_mainLay;
  QHBoxLayout *m_visLay;

  QLabel *m_curWorkTimeLabel;
  QLabel *m_intervalLabel;
  QLabel *m_cycleLabel;

  StopButton  *m_stopBtn;

  QPushButton *m_nextIntervalBtn;

  AccessManager::AccessLevel m_accessLevel;
};
} // namespace deepvaccontrol

#endif // HEAD_H
