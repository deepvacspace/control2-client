#ifndef SETTINGSDIFF_H
#define SETTINGSDIFF_H

#include <QDialog>

namespace control2 {
class SettingsDiff : public QDialog {
  Q_OBJECT
public:
  explicit SettingsDiff(QMap<QString, float> const &local, QMap<QString, float> const &plc, QWidget *parent = nullptr);
  ~SettingsDiff() override = default;

private:
  QMap<QString, float> m_local;
  QMap<QString, float> m_plc;
};
} // namespace control2

#endif // SETTINGSDIFF_H
