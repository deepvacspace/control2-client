#include "filesavedialog.h"

#include <QLabel>

#include "messagedialog.h"
#include "utils.h"

namespace deepvaccontrol {
FileSaveDialog::FileSaveDialog(QList<QString> pathList, QWidget *parent) : QDialog{parent} {
  m_drivePathSelect = new DrivePathSelect(pathList, this);

  auto *mainLay = new QVBoxLayout(this);

  auto *fileLay = new QHBoxLayout();
  fileLay->setContentsMargins(0, 0, 0, 0);
  fileLay->addWidget(new QLabel(tr("Название файла:"), this));
  fileNameEdit = new LineEditScreen(this, VirtualKeyboard::EN);
  fileLay->addWidget(fileNameEdit);

  auto *btnLay = new QHBoxLayout();
  btnLay->setContentsMargins(0, 0, 0, 0);
  m_acceptBtn = new QPushButton(tr("Сохранить"), this);
  m_rejectBtn = new QPushButton(tr("Отмена"), this);
  m_acceptBtn->setFixedHeight(40 * utils::ssf());
  m_rejectBtn->setFixedHeight(40 * utils::ssf());
  btnLay->addWidget(m_acceptBtn);
  btnLay->addWidget(m_rejectBtn);

  mainLay->addWidget(m_drivePathSelect);
  mainLay->addSpacing(10 * utils::ssf());
  mainLay->addLayout(fileLay);
  mainLay->addStretch();
  mainLay->addLayout(btnLay);

  connect(m_rejectBtn, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_acceptBtn, &QPushButton::clicked, this, [this]() {
    if (fileNameEdit->text().isEmpty()) {
      auto *msgDlg = new MessageDialog(tr("Предупреждение"), tr("Невозможно сохранить файл с пустым названием."), this);
      msgDlg->open();
    } else {
      m_resultFilePath = m_drivePathSelect->getSelectedPath() + "/" + fileNameEdit->text();
      accept();
    }
  });
  m_rejectBtn->setFocus();

  setGeometry(100, 100, 500 * utils::ssf(), 250 * utils::ssf());
  setWindowTitle(tr("Сохранение файла"));
  setModal(true);
  setAttribute(Qt::WA_DeleteOnClose);
}

} // namespace deepvaccontrol
