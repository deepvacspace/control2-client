#include "fileloaddialog.h"

#include <QGridLayout>
#include <QPushButton>
#include <confdialog.h>

#include "drivepathselect.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {
FileLoadDialog::FileLoadDialog(QList<QString> pathList, QString const &filter, QWidget *parent)
    : QDialog{parent}, m_fileFilter{filter} {
  auto *drivePathSelect = new DrivePathSelect(pathList, this);
  m_fileSysModel        = new QFileSystemModel(this);
  m_sortedModel         = new QSortFilterProxyModel(this);
  m_fileListView        = new QListView(this);

  selectPath(drivePathSelect->getSelectedPath());

  auto *mainLay  = new QGridLayout(this);
  auto *openBtn  = new QPushButton(tr("Select"), this);
  auto *delBtn   = new QPushButton(tr("Delete"), this);
  auto *closeBtn = new QPushButton(tr("Cancel"), this);

  openBtn->setFixedHeight(theme::dp(75));
  delBtn->setFixedHeight(theme::dp(75));
  closeBtn->setFixedHeight(theme::dp(75));

  mainLay->addWidget(drivePathSelect, 0, 0, 1, 3);
  mainLay->addWidget(m_fileListView, 1, 0, 1, 3);
  mainLay->addWidget(openBtn, 2, 0);
  mainLay->addWidget(delBtn, 2, 1);
  mainLay->addWidget(closeBtn, 2, 2);

  m_fileListView->setStyleSheet(utils::readStyleFromFile(":/styles/verticalScroll.qss"));

  connect(m_fileListView, &QListView::doubleClicked, this, &FileLoadDialog::handleFileSelection);
  connect(closeBtn, &QPushButton::clicked, this, &FileLoadDialog::reject);
  connect(openBtn, &QPushButton::clicked, this, [this] {
    handleFileSelection(m_fileListView->currentIndex());
  });
  connect(delBtn, &QPushButton::clicked, this, [this] {
    handleFileRemoving(m_fileListView->currentIndex());
  });
  connect(drivePathSelect, &DrivePathSelect::selectedPathChanged, this, &FileLoadDialog::selectPath);

  setGeometry(100, 100, 500 * utils::ssf(), 250 * utils::ssf());
  setWindowTitle(tr("Select file"));
  setModal(true);
  setAttribute(Qt::WA_DeleteOnClose);
}

void FileLoadDialog::handleFileSelection(QModelIndex const &index) {
  if (index.isValid()) {
    m_resultFilePath = m_fileSysModel->filePath(m_sortedModel->mapToSource(index));
    accept();
  } else {
    reject();
  }
}

void FileLoadDialog::handleFileRemoving(QModelIndex const &index) {
  if (!index.isValid()) {
    return;
  }

  auto *confDlg
    = new ConfDialog(tr("Confirmation"), tr("Do you really want to delete the file?"), tr("Yes"), tr("No"), this);
  connect(confDlg, &ConfDialog::finished, this, [this, index]() {
    QString const filePath{m_fileSysModel->filePath(m_sortedModel->mapToSource(index))};
    QFile         file(filePath);
    file.remove();
  });

  confDlg->show();
}

void FileLoadDialog::selectPath(QString const &path) {
  m_fileSysModel->setRootPath(path);
  m_fileSysModel->setFilter(QDir::Files);
  m_fileSysModel->setNameFilters({m_fileFilter});
  m_sortedModel->setSourceModel(m_fileSysModel);
  m_sortedModel->sort(0, Qt::SortOrder::DescendingOrder);
  m_fileListView->setModel(m_sortedModel);
  m_fileListView->setRootIndex(m_sortedModel->mapFromSource(m_fileSysModel->index(path)));
}
}
