#ifndef FILESAVEDIALOGPATHPROVIDER_H
#define FILESAVEDIALOGPATHPROVIDER_H

#include "filesavedialog.h"
#include "logger.h"
#include "pathprovider.h"

namespace control2 {
class FileSaveDialogPathProvider : public PathProvider {
public:
  FileSaveDialogPathProvider(QStringList pathList, Logger *logger, QObject *parent = nullptr)
      : PathProvider{logger, parent}, m_pathList(pathList) {
  }
  ~FileSaveDialogPathProvider() = default;

private:
  QStringList m_pathList;

  // PathProvider interface
public:
  void getPathAsync(QWidget *parent, QString const &, std::function<void(QString const &)> callback) override {
    auto *dialog = new FileSaveDialog(m_pathList, parent);
    connect(dialog, &QDialog::accepted, this, [dialog, callback, this]() {
      callback(dialog->getFilePath());
    });
    dialog->open();
  }
};
} // namespace control2

#endif // FILESAVEDIALOGPATHPROVIDER_H
