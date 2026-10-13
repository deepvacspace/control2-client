#ifndef DERIVEDVARIABLESDIALOG_H
#define DERIVEDVARIABLESDIALOG_H

#include <QColor>
#include <QDialog>

#include "experimentlibrary.h"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace deepvaccontrol {

class LineEditNum;
class LineEditScreen;

/**
 * @brief Create / delete derived variables: reusable formulas shown as extra channels in every
 * experiment that has their source channels (difference, rate of change, rolling standard
 * deviation, cumulative integral, custom expression).
 */
class DerivedVariablesDialog : public QDialog {
  Q_OBJECT
public:
  DerivedVariablesDialog(QVector<ChannelInfo> const &channels, QString const &userName, QWidget *parent = nullptr);

  [[nodiscard]] bool changed() const;

private:
  void refreshList();
  void updateForm();
  void add();
  void deleteSelected();

  QVector<ChannelInfo> m_channels;
  QString              m_userName;
  bool                 m_changed{false};
  QColor               m_color;

  QListWidget    *m_list{nullptr};
  LineEditScreen *m_nameEdit{nullptr};
  QComboBox      *m_typeCombo{nullptr};
  QComboBox      *m_sourceCombo{nullptr};
  QComboBox      *m_source2Combo{nullptr};
  LineEditNum    *m_windowEdit{nullptr};
  LineEditScreen *m_expressionEdit{nullptr};
  QComboBox      *m_insertCombo{nullptr};
  QPushButton    *m_colorBtn{nullptr};
  QList<QWidget *> m_sourceRow;
  QList<QWidget *> m_source2Row;
  QList<QWidget *> m_windowRow;
  QList<QWidget *> m_expressionRow;
};

inline bool DerivedVariablesDialog::changed() const {
  return m_changed;
}

} // namespace deepvaccontrol

#endif // DERIVEDVARIABLESDIALOG_H
