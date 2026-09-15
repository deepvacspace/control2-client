#ifndef FILEHANDLER_H
#define FILEHANDLER_H

#include <QFile>

#include "pathprovider.h"
#include "logger.h"

namespace deepvaccontrol {
class FileHandler {
public:
  virtual ~FileHandler() = default;

  // Сохранение с выбором пути.
  bool saveToFile(PathProvider *pathProvider, QWidget *parent = nullptr, QString const &filter = QString());
  void saveToFileAsync(PathProvider *pathProvider, QWidget *parent, QString const &filter = QString());

  // Загрузка с выбором пути.
  bool loadFromFile(PathProvider *pathProvider, QWidget *parent = nullptr, QString const &filter = QString());
  void loadFromFileAsync(PathProvider *pathProvider, QWidget *parent, QString const &filter = QString());

protected:
  // Сохранение данных по конкретному пути.
  // Обработка общий ошибок откртия файла.
  bool saveToFileImpl(QString const &filePath, Logger *logger);

  // Загрузка данных из файла по конкретному пути.
  // Обработка общий ошибок откртия файла.
  bool loadFromFileImpl(QString const &filePath, Logger *logger);

  // Абстрактный класс записи данных класса в файл.
  virtual bool writeData(QFile &file) = 0;
  // Абстрактный класс чтения данных класса из файла.
  virtual bool readData(QFile &file)  = 0;
};
} // namespace deepvaccontrol

#endif // FILEHANDLER_H
