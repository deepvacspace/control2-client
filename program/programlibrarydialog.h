#ifndef PROGRAMLIBRARYDIALOG_H
#define PROGRAMLIBRARYDIALOG_H

#include <QDialog>
#include <optional>

#include "testprofilesstore.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace deepvaccontrol {

class LineEditScreen;
class ProgramModel;

// Saved programs (test profiles database, shared with paired computers) <-> the program editor model.
namespace programlibrary {
// summary steps (temperature / pressure / duration / label) of the model's intervals
QVector<storage::TestProfileStep> stepsFromModel(ProgramModel const &model, QStringList const &labels);
// loads a saved program into the model: the full program when stored, else one interval per step
bool loadIntoModel(storage::TestProfile const &profile, ProgramModel &model, QString *error = nullptr);
} // namespace programlibrary

/**
 * @brief Pick a saved program to open, or delete one.
 */
class ProgramLibraryDialog : public QDialog {
  Q_OBJECT
public:
  explicit ProgramLibraryDialog(QWidget *parent = nullptr);

  [[nodiscard]] std::optional<storage::TestProfile> selected() const;

private:
  void refresh();
  void showPreview();

  QListWidget                       *m_list{nullptr};
  QLabel                            *m_preview{nullptr};
  QPushButton                       *m_openBtn{nullptr};
  QPushButton                       *m_deleteBtn{nullptr};
  std::optional<storage::TestProfile> m_selected;
};

/**
 * @brief Save the program being edited to the library under a name, with a label per interval.
 */
class ProgramSaveDialog : public QDialog {
  Q_OBJECT
public:
  ProgramSaveDialog(ProgramModel const &model, QString const &name, QStringList const &labels, QString const &userName,
                    QWidget *parent = nullptr);

  [[nodiscard]] std::optional<storage::TestProfile> saved() const;

private:
  void save(bool replaceExisting);

  ProgramModel const                 &m_model;
  QString                             m_userName;
  LineEditScreen                     *m_nameEdit{nullptr};
  QTableWidget                       *m_table{nullptr};
  std::optional<storage::TestProfile> m_saved;
};

} // namespace deepvaccontrol

#endif // PROGRAMLIBRARYDIALOG_H
