#ifndef CALENDARDIALOGPATHPROVIDER_H
#define CALENDARDIALOGPATHPROVIDER_H

#include <QObject>

#include "calendardialog.h"
#include "logger.h"
#include "pathprovider.h"

namespace control2 {
/**
 * @brief Async path provider with calendar dialog.
 */
class CalendarDialogPathProvider : public PathProvider {
  Q_OBJECT
public:
  explicit CalendarDialogPathProvider(QString const &dirPath, Logger *logger, QObject *parent = nullptr)
      : PathProvider{logger, parent}, m_dirPath{dirPath} {
  }
  ~CalendarDialogPathProvider() override = default;

private:
  QString m_dirPath;

  // PathProvider interface
public:
  void getPathAsync(QWidget *parent, QString const &filter, std::function<void(QString const &)> callback) override {
    auto *dialog = new CalendarDialog(m_dirPath, parent);
    connect(dialog, &CalendarDialog::accepted, this, [dialog, callback, this]() {
      // callback(dialog->getFilePath());
      dialog->deleteLater(); // Удаляем диалог после завершения
    });
    dialog->open();
  }
};
} // namespace control2

#endif // CALENDARDIALOGPATHPROVIDER_H
