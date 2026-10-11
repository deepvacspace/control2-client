#include "settingsrecoverydialog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "settingsmanager.h"
#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {

SettingsRecoveryDialog::SettingsRecoveryDialog(QString const &settingsPath, QString const &problem, QWidget *parent)
    : QDialog{parent}, m_settingsPath{QFileInfo(settingsPath).absoluteFilePath()} {
  double const ssf{utils::ssf()};
  setWindowTitle(tr("Deepvac Control"));

  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(22 * ssf, 20 * ssf, 22 * ssf, 18 * ssf);
  mainLay->setSpacing(14 * ssf);

  auto *top = new QHBoxLayout();
  top->setSpacing(14 * ssf);
  auto *icon = new QLabel(this);
  icon->setPixmap(theme::dialogIcon(QStringLiteral("error"), static_cast<int>(34 * ssf)));
  top->addWidget(icon, 0, Qt::AlignTop);
  auto *texts = new QVBoxLayout();
  texts->setSpacing(8 * ssf);
  auto *title = new QLabel(tr("The settings file cannot be used"), this);
  title->setObjectName("cardTitle");
  QFont titleFont = title->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
  title->setFont(titleFont);
  texts->addWidget(title);
  m_problem = new QLabel(this);
  m_problem->setWordWrap(true);
  m_problem->setTextInteractionFlags(Qt::TextSelectableByMouse);
  texts->addWidget(m_problem);
  auto *hint = new QLabel(tr("Restore one of the backups below, or fix or replace the file and try again."), this);
  hint->setWordWrap(true);
  hint->setProperty("muted", true);
  texts->addWidget(hint);
  top->addLayout(texts, 1);
  mainLay->addLayout(top);

  m_backups = new QListWidget(this);
  m_backups->setMinimumHeight(static_cast<int>(150 * ssf));
  mainLay->addWidget(m_backups, 1);

  auto *buttons = new QHBoxLayout();
  buttons->setSpacing(10 * ssf);
  auto addButton = [&](QString const &text) {
    auto *button = new QPushButton(text, this);
    button->setMinimumSize(static_cast<int>(120 * ssf), static_cast<int>(36 * ssf));
    buttons->addWidget(button);
    return button;
  };
  QPushButton *exitBtn   = addButton(tr("Exit"));
  QPushButton *folderBtn = addButton(tr("Open folder"));
  buttons->addStretch();
  QPushButton *retryBtn = addButton(tr("Try again"));
  m_restoreBtn          = addButton(tr("Restore selected"));
  m_restoreBtn->setObjectName("primaryButton");
  mainLay->addLayout(buttons);

  connect(exitBtn, &QPushButton::clicked, this, [this] { done(EXIT); });
  connect(folderBtn, &QPushButton::clicked, this, [this] {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_settingsPath).absolutePath()));
  });
  connect(retryBtn, &QPushButton::clicked, this, [this] { done(RETRY); });
  connect(m_restoreBtn, &QPushButton::clicked, this, [this] { done(RESTORE); });
  connect(m_backups, &QListWidget::currentRowChanged, this, [this] {
    QListWidgetItem const *item = m_backups->currentItem();
    m_restoreBtn->setEnabled(item && (item->flags() & Qt::ItemIsEnabled));
  });
  connect(m_backups, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
    if (item->flags() & Qt::ItemIsEnabled) {
      done(RESTORE);
    }
  });

  setProblem(problem);
  setMinimumWidth(static_cast<int>(620 * ssf));
}

void SettingsRecoveryDialog::setProblem(QString const &problem) {
  m_problem->setText(tr("%1\n%2").arg(QDir::toNativeSeparators(m_settingsPath), problem));
  refreshBackups();
}

void SettingsRecoveryDialog::refreshBackups() {
  // settings_backup.json and the timestamped settings_backup_*.json, newest first; the unusable ones say why
  QDir const          folder = QFileInfo(m_settingsPath).absoluteDir();
  QFileInfoList const files  = folder.entryInfoList({QStringLiteral("settings_backup*.json")}, QDir::Files, QDir::Time);
  m_backups->clear();
  for (QFileInfo const &file : files) {
    QString const problem = SettingsManager::fileProblem(file.absoluteFilePath());
    QString const when    = file.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    auto *item = new QListWidgetItem(problem.isEmpty() ? tr("%1   ·   saved %2").arg(file.fileName(), when)
                                                       : tr("%1   ·   saved %2   ·   cannot be used: %3").arg(file.fileName(), when, problem),
                                     m_backups);
    item->setData(Qt::UserRole, file.absoluteFilePath());
    if (!problem.isEmpty()) {
      item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
  }
  if (m_backups->count() == 0) {
    auto *item = new QListWidgetItem(tr("No backup found next to the settings file."), m_backups);
    item->setFlags(Qt::NoItemFlags);
  }
  // the newest usable one preselected
  m_backups->setCurrentRow(-1);
  for (int i{0}; i < m_backups->count(); ++i) {
    if (m_backups->item(i)->flags() & Qt::ItemIsEnabled) {
      m_backups->setCurrentRow(i);
      break;
    }
  }
  QListWidgetItem const *item = m_backups->currentItem();
  m_restoreBtn->setEnabled(item && (item->flags() & Qt::ItemIsEnabled));
}

QString SettingsRecoveryDialog::selectedBackup() const {
  QListWidgetItem const *item = m_backups->currentItem();
  return item ? item->data(Qt::UserRole).toString() : QString();
}

void SettingsRecoveryDialog::reject() {
  done(EXIT);
}

} // namespace deepvaccontrol
