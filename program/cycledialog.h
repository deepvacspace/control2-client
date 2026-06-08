#ifndef CYCLEDIALOG_H
#define CYCLEDIALOG_H

#include <QDialog>

namespace control2 {
class CycleDialog : public QDialog {
  Q_OBJECT
public:
  explicit CycleDialog(QVariantMap &cycleParams, int interval, QWidget *parent = nullptr);
  ~CycleDialog() override = default;

private:
  QVariantMap &m_cycleParams;
};
} // namespace control2

#endif // CYCLEDIALOG_H
