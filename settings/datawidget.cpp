#include "datawidget.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "backupservice.h"
#include "constants.h"
#include "dialogs/confdialog.h"
#include "dialogs/messagedialog.h"
#include "sqlitedb.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {

using storage::BackupService;

namespace {
QString const DB_ROLE_NAME{"db"};

QString fileSize(qint64 bytes) {
  if (bytes >= 1024 * 1024) {
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
  }
  return QStringLiteral("%1 kB").arg(std::max<qint64>(1, bytes / 1024));
}

QString describe(QString const &fileName) {
  static QMap<QString, QString> const names{{storage::RUNS_DB, QObject::tr("Experiments")},
                                            {storage::TEST_PROFILES_DB, QObject::tr("Test profiles")},
                                            {QStringLiteral("deepvac_derived_variables.sqlite3"), QObject::tr("Derived variables")},
                                            {storage::ANNOTATIONS_DB, QObject::tr("Annotations and variable rules")},
                                            {storage::COLLAB_HOST_DB, QObject::tr("Sharing host (paired devices, shared items)")},
                                            {storage::SHARED_RUNS_DB, QObject::tr("Experiments shared by other computers (list)")}};
  return names.value(fileName, fileName);
}
}

DataWidget::DataWidget(ResHolder *resHolder, QWidget *parent) : QWidget{parent}, m_resHolder{resHolder} {
  auto *lay = new QVBoxLayout(this);
  lay->setContentsMargins(3, 3, 3, 3);


  m_tree = new QTreeWidget(this);
  m_tree->setHeaderLabels({tr("Database / backup"), tr("Size"), tr("Contents")});
  m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  m_tree->setStyleSheet(QStringLiteral("QTreeWidget::item { height: %1px; }").arg(static_cast<int>(32 * utils::ssf())));
  lay->addWidget(m_tree, 1);

  auto *buttons    = new QHBoxLayout();
  auto *backupBtn  = new QPushButton(tr("Back up now"), this);
  auto *restoreBtn = new QPushButton(tr("Restore selected backup…"), this);
  auto *refreshBtn = new QPushButton(tr("Refresh"), this);
  for (QPushButton *button : {backupBtn, restoreBtn, refreshBtn}) {
    button->setFixedHeight(theme::dp(UI_BTN_HEIGHT));
    button->setFocusPolicy(Qt::NoFocus);
    buttons->addWidget(button);
  }
  buttons->addStretch(1);
  lay->addLayout(buttons);

  connect(backupBtn, &QPushButton::clicked, this, &DataWidget::backupNow);
  connect(restoreBtn, &QPushButton::clicked, this, &DataWidget::restoreSelected);
  connect(refreshBtn, &QPushButton::clicked, this, &DataWidget::refresh);
}

void DataWidget::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  refresh();
}

void DataWidget::refresh() {
  m_tree->clear();
  auto const backups = BackupService::listBackups();
  for (QString const &fileName : BackupService::databases()) {
    auto *dbItem = new QTreeWidgetItem(m_tree);
    dbItem->setText(0, fileName);
    dbItem->setText(1, fileSize(QFileInfo(storage::dbPath(fileName)).size()));
    dbItem->setText(2, describe(fileName));
    QFont font = dbItem->font(0);
    font.setBold(true);
    dbItem->setFont(0, font);
    for (QFileInfo const &backup : backups.value(QFileInfo(fileName).completeBaseName())) {
      auto *item = new QTreeWidgetItem(dbItem);
      item->setText(0, backup.fileName());
      item->setText(1, fileSize(backup.size()));
      item->setText(2, backup.lastModified().toString("yyyy-MM-dd HH:mm"));
      item->setData(0, Qt::UserRole, backup.absoluteFilePath());
      item->setData(0, Qt::UserRole + 1, fileName);
    }
  }
}

void DataWidget::backupNow() {
  auto     result = std::make_shared<QPair<QStringList, QStringList>>(); // created, errors
  QThread *thread = QThread::create([result] {
    result->first = BackupService::backupAll(true, &result->second);
  });
  connect(thread, &QThread::finished, this, [this, thread, result] {
    thread->deleteLater();
    refresh();
    (new MessageDialog(tr("Back up"),
                       result->second.isEmpty() ? tr("Backed up %n database(s).", "", result->first.size())
                                                : tr("Backup failed: %1").arg(result->second.join("; "))))
      ->open();
  });
  thread->start();
}

void DataWidget::restoreSelected() {
  QTreeWidgetItem *item = m_tree->currentItem();
  if (!item || item->data(0, Qt::UserRole).toString().isEmpty()) {
    (new MessageDialog(tr("Restore"), tr("Select a backup below a database first.")))->open();
    return;
  }
  QString const backupPath = item->data(0, Qt::UserRole).toString();
  QString const fileName   = item->data(0, Qt::UserRole + 1).toString();

  auto *confirm = new ConfDialog(tr("Restore"),
                                 tr("Replace %1 with the backup %2?\nThe current file is kept as a \"pre-restore\" backup.")
                                   .arg(fileName, QFileInfo(backupPath).fileName()),
                                 tr("Restore"),
                                 tr("Cancel"),
                                 this);
  connect(confirm, &QDialog::accepted, this, [this, backupPath, fileName] {
    QString    error;
    bool const ok = BackupService::restore(fileName, backupPath, &error);
    refresh();
    (new MessageDialog(tr("Restore"),
                       ok ? tr("Restored. Reopen the Analysis page to see the restored data.")
                          : tr("Restore failed: %1").arg(error)))
      ->open();
  });
  confirm->open();
}

}
