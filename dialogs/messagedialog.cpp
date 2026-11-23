#include "messagedialog.h"

#include <QLabel>
#include <QTimer>

#include "theme.h"
#include "utils.h"

namespace deepvaccontrol {
namespace {
// icon from the title: errors red, warnings amber, anything else (confirmations, information) blue
QString iconKind(QString const &title) {
  QString const t = title.toLower();
  for (char const *word : {"error", "ошибка", "failed", "критическ"}) {
    if (t.contains(QString::fromUtf8(word))) {
      return QStringLiteral("error");
    }
  }
  for (char const *word : {"warning", "внимание", "предупреждение", "attention", "caution"}) {
    if (t.contains(QString::fromUtf8(word))) {
      return QStringLiteral("warning");
    }
  }
  return QStringLiteral("info");
}
}

MessageDialog::MessageDialog(QString const &title, QString const &message, QWidget *parent) : QDialog{parent} {
  double const ssf{utils::ssf()};
  auto        *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(theme::dp(40), theme::dp(36), theme::dp(40), theme::dp(32));
  mainLay->setSpacing(theme::dp(32));

  // icon | title and message
  auto *top = new QHBoxLayout();
  top->setSpacing(theme::dp(28));
  m_iconLabel = new QLabel(this);
  m_iconLabel->setPixmap(theme::dialogIcon(iconKind(title), static_cast<int>(34 * ssf)));
  top->addWidget(m_iconLabel, 0, Qt::AlignTop);
  auto *texts = new QVBoxLayout();
  texts->setSpacing(theme::dp(12));
  auto *titleLabel = new QLabel(title, this);
  titleLabel->setObjectName("cardTitle");
  QFont titleFont = titleLabel->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
  titleLabel->setFont(titleFont);
  titleLabel->setWordWrap(true);
  texts->addWidget(titleLabel);
  auto *msgLabel = new QLabel(message, this);
  msgLabel->setWordWrap(true);
  msgLabel->setMinimumWidth(theme::dp(600));
  texts->addWidget(msgLabel);
  top->addLayout(texts, 1);
  mainLay->addLayout(top);

  // buttons on the right, the main one last
  m_btnLayout = new QHBoxLayout();
  m_btnLayout->setSpacing(theme::dp(20));
  m_btnLayout->addStretch();
  m_closeBtn = new QPushButton(tr("Close"), this);
  m_closeBtn->setObjectName("primaryButton");
  m_closeBtn->setMinimumSize(theme::dp(206), theme::dp(67));
  m_btnLayout->addWidget(m_closeBtn);
  mainLay->addLayout(m_btnLayout);

  setWindowTitle(title);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlag(Qt::WindowStaysOnTopHint);
  setMinimumWidth(theme::dp(825));
  setMaximumWidth(theme::dp(1275));
  adjustSize();

  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
}

void MessageDialog::setIconKind(QString const &kind) {
  m_iconLabel->setPixmap(theme::dialogIcon(kind, static_cast<int>(34 * utils::ssf())));
}

void MessageDialog::closeEvent(QCloseEvent *) {
  emit finished(0); // QDialog::Rejected == close dialog
}
}

void deepvaccontrol::MessageDialog::open(int visDurationMs) {
  QDialog::open();

  if (0 != visDurationMs) {
    m_closeTimer = new QTimer(this);
    m_closeTimer->setSingleShot(true);
    connect(m_closeTimer, &QTimer::timeout, this, [this]() {
      done(0);
    });
    m_closeTimer->start(visDurationMs);
  }
}
