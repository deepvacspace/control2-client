#ifndef FILELOADDIALOGPATHPROVIDER_H
#define FILELOADDIALOGPATHPROVIDER_H

#include "fileloaddialog.h"
#include "logger.h"
#include "pathprovider.h"

namespace deepvaccontrol {
/**
 * @brief Async path provider with dialog.
 */
class FileLoadDialogPathProvider : public PathProvider {
  Q_OBJECT
public:
  FileLoadDialogPathProvider(QStringList pathList, Logger *logger, QObject *parent = nullptr)
      : PathProvider{logger, parent}, m_pathList(pathList) {
  }
  ~FileLoadDialogPathProvider() override = default;

private:
  QStringList m_pathList;

  // PathProvider interface
public:
  void getPathAsync(QWidget *parent, QString const &filter, std::function<void(QString const &)> callback) override {
    auto *dialog = new FileLoadDialog(m_pathList, filter, parent);
    connect(dialog, &FileLoadDialog::accepted, this, [dialog, callback, this]() {
      callback(dialog->getFilePath());
    });
    dialog->open();
  };
};
} // namespace deepvaccontrol

#endif // FILELOADDIALOGPATHPROVIDER_H
