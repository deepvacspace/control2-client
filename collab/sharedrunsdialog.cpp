#include "sharedrunsdialog.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "collabmanager.h"
#include "constants.h"
#include "experimentlibrary.h"
#include "utils.h"
#include "theme.h"

namespace deepvaccontrol {

SharedRunsDialog::SharedRunsDialog(CollabManager *collab, QWidget *parent) : QDialog{parent}, m_collab{collab} {
  setWindowTitle(tr("Shared experiments"));
  resize(theme::dp(1875), theme::dp(1050));

  auto *lay = new QVBoxLayout(this);
  m_list    = new QTreeWidget(this);
  m_list->setRootIsDecorated(false);
  m_list->setHeaderLabels({tr("Experiment"), tr("Chamber"), tr("Start"), tr("Duration"), tr("Shared by"), tr("Here")});
  m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  for (int c{1}; c < 6; ++c) {
    m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
  }
  m_list->setStyleSheet(QStringLiteral("QTreeWidget::item { height: %1px; }").arg(static_cast<int>(32 * utils::ssf())));
  lay->addWidget(m_list, 1);

  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  lay->addWidget(m_status);

  auto *buttons  = new QHBoxLayout();
  m_syncBtn      = new QPushButton(tr("Sync now"), this);
  m_downloadBtn  = new QPushButton(tr("Download"), this);
  auto *closeBtn = new QPushButton(tr("Close"), this);
  for (QPushButton *button : {m_syncBtn, m_downloadBtn, closeBtn}) {
    button->setFixedHeight(theme::dp(UI_BTN_HEIGHT));
    button->setFocusPolicy(Qt::NoFocus);
  }
  buttons->addWidget(m_syncBtn);
  buttons->addWidget(m_downloadBtn);
  buttons->addStretch(1);
  buttons->addWidget(closeBtn);
  lay->addLayout(buttons);

  connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_syncBtn, &QPushButton::clicked, m_collab, &CollabManager::syncNow);
  connect(m_downloadBtn, &QPushButton::clicked, this, [this] {
    if (QTreeWidgetItem *item = m_list->currentItem()) {
      m_status->setText(tr("Downloading…"));
      m_collab->downloadRun(item->data(0, Qt::UserRole).toString());
    }
  });
  connect(m_list, &QTreeWidget::currentItemChanged, this, [this] {
    QTreeWidgetItem *item = m_list->currentItem();
    m_downloadBtn->setEnabled(item && item->data(0, Qt::UserRole + 1).toBool() && !m_collab->isBusy());
  });
  connect(m_collab, &CollabManager::syncFinished, this, [this](bool, QString const &summary) {
    m_status->setText(summary);
    refresh();
  });
  connect(m_collab, &CollabManager::downloadFinished, this, [this](bool, QString const &message) {
    m_status->setText(message);
    refresh();
  });
  connect(m_collab, &CollabManager::stateChanged, this, [this] {
    m_syncBtn->setEnabled(!m_collab->isBusy() && m_collab->isConnected());
  });

  refresh();
  m_syncBtn->setEnabled(!m_collab->isBusy() && m_collab->isConnected());
}

void SharedRunsDialog::refresh() {
  m_list->clear();
  for (collab::SharedRun const &run : collab::CollabSync::sharedRuns()) {
    auto *item = new QTreeWidgetItem(m_list);
    item->setText(0, run.metadata.value("id").toString());
    item->setText(1, run.metadata.value("chamber").toString());
    item->setText(2, run.metadata.value("start_time").toString());
    item->setText(3, ExperimentLibrary::formatDuration(run.metadata.value("duration_s").toDouble()));
    item->setText(4, run.metadata.value("shared_by").toString());
    item->setText(5, run.local ? tr("yes") : (run.hasData ? tr("available") : tr("list only")));
    item->setData(0, Qt::UserRole, run.uid);
    item->setData(0, Qt::UserRole + 1, run.hasData && !run.local);
  }
  if (m_list->topLevelItemCount() == 0) {
    m_status->setText(tr("Nothing shared yet. Press \"Sync now\" to fetch the list from the host."));
  }
  m_downloadBtn->setEnabled(false);
}

}
