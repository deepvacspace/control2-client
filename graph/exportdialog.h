#ifndef EXPORTDIALOG_H
#define EXPORTDIALOG_H

#include <QDateTime>
#include <QDialog>

#include "dataholder.h"
#include "resholder.h"

class QButtonGroup;
class QDateTimeEdit;
class QLabel;
class QPushButton;

namespace deepvaccontrol {

/**
 * @brief Chart export: what (chart image, data, experiment report or bundle), of which range (the visible view, the
 * whole experiment shown, its day or a custom range) and where (file browser).
 */
class ExportDialog : public QDialog {
  Q_OBJECT
public:
  enum class Range : uint8_t {
    VIEW,
    EXPERIMENT,
    DAY,
    CUSTOM
  };

  // day: of the data shown; canReport / canBundle: what the shown data offers; canUseFiles: the day and custom ranges
  // (read from the recorded files) make sense for it
  ExportDialog(ResHolder *resHolder, QDate const &day, bool canReport, bool canBundle, bool canUseFiles, QWidget *parent = nullptr);

  // "png", "bmp", "jpeg", "csv", "txt", "html", "pdf" or "json"
  [[nodiscard]] QString   format() const;
  [[nodiscard]] Range     range() const;
  // DAY and CUSTOM
  [[nodiscard]] QDateTime from() const;
  [[nodiscard]] QDateTime to() const;
  // chosen in the file browser, with its extension
  [[nodiscard]] QString   filePath() const;
  // the recorded interval files of [from, to] (DAY and CUSTOM)
  [[nodiscard]] std::shared_ptr<DataHolder> readInterval() const;

  [[nodiscard]] static bool isImage(QString const &format);
  [[nodiscard]] static bool isData(QString const &format);

private:
  void updateState();
  void choosePath();

  ResHolder     *m_resHolder;
  QDate          m_day;
  QButtonGroup  *m_formats{nullptr};
  QButtonGroup  *m_ranges{nullptr};
  QDateTimeEdit *m_fromEdit{nullptr};
  QDateTimeEdit *m_toEdit{nullptr};
  QLabel        *m_rangeNote{nullptr};
  QPushButton   *m_previewBtn{nullptr};
  QString        m_filePath;
};

} // namespace deepvaccontrol

#endif // EXPORTDIALOG_H
