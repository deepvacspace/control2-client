#ifndef FORCEOUTSINGLEWIDGET_H
#define FORCEOUTSINGLEWIDGET_H

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QWidget>

#include "lineeditnum.h"
#include "systemvalue.h"

namespace deepvaccontrol {
class ForceOutSingleWidget : public QWidget {
  Q_OBJECT
public:
  enum class ForceType {
    ON_OFF = 1,
    DUTY   = 2
  };
  explicit ForceOutSingleWidget(int            index,
                                QString const &stateLabel,
                                SystemValue   *sysValue,
                                ForceType      type   = ForceType::ON_OFF,
                                QWidget       *parent = nullptr);
  void       resetSelection();
  void       resetSetBtn();
  QByteArray getRawData() const;

signals:
  void generateRaw(int /*always 1 here*/ count, QByteArray data);

private:
  void initGui();
  int  m_index;

  QLabel      *m_valueLabel{nullptr};
  QSlider     *m_slider{nullptr};
  QPushButton *m_setBtn{nullptr};

  ForceType    type;
  LineEditNum *duty{nullptr};
  LineEditNum *period{nullptr};

  uint plcType = 0;
};

inline void ForceOutSingleWidget::resetSelection() {
  plcType = 0;
  m_slider->setValue(0);
  resetSetBtn();
  m_valueLabel->setText("");
}

inline void ForceOutSingleWidget::resetSetBtn() {
  m_setBtn->setVisible(false);
}
} // namespace deepvaccontrol

#endif // FORCEOUTSINGLEWIDGET_H
