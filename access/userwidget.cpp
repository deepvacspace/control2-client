#include "userwidget.h"

#include <QEvent>
#include <QCoreApplication>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

#include "constants.h"
#include "messagedialog.h"
#include "usereditdialog.h"
#include "theme.h"
#include "utils.h"
#include "virtualkeyboard.h"

namespace deepvaccontrol {
UserWidget::UserWidget(Logger *logger, AccessManager *accessManager, QWidget *parent)
    : QWidget{parent}, m_logger{logger}, m_accessManager{accessManager} {
  initGui();

  connect(m_addUserBtn, &QPushButton::clicked, this, [this] {
    auto editDialog = new UserEditDialog(m_accessManager, "", this);
    editDialog->open();
  });

  connect(m_editUserBtn, &QPushButton::clicked, this, [this] {
    auto editDialog = new UserEditDialog(m_accessManager, getSelectedUser(), this);
    editDialog->open();
  });

  connect(m_delUserBtn, &QPushButton::clicked, this, [this]() {
    QString const selectedUser{getSelectedUser()};
    if (!selectedUser.isEmpty()) {
      m_accessManager->removeUser(selectedUser);
    }
  });

  connect(m_selectUserBtn, &QPushButton::clicked, this, &UserWidget::switchUser);
  connect(m_usersList, &QTableWidget::cellDoubleClicked, this, &UserWidget::switchUser);
  connect(m_usersList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &UserWidget::updateBtnsEnable);
  connect(m_accessManager, &AccessManager::userListChanged, this, &UserWidget::updateUsersList);
  connect(m_accessManager, &AccessManager::userSelected, this, &UserWidget::updateBtnsEnable);

  updateUsersList();
}

namespace {
// level names as in the user editor
QString levelName(int level) {
  switch (level) {
    case AccessManager::AccessLevel::SPECTATOR_0:
      return UserWidget::tr("Observer");
    case AccessManager::AccessLevel::USER_START_1:
      return UserWidget::tr("Level 1");
    case AccessManager::AccessLevel::USER_EDIT_2:
      return UserWidget::tr("Level 2");
    default:
      return UserWidget::tr("System Engineer");
  }
}
} // namespace

void UserWidget::initGui() {
  double const ssf{utils::ssf()};
  auto        *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);
  mainLay->setSpacing(10 * ssf);

  auto makeCard = [this, ssf](QString const &title) {
    auto *card = new QFrame(this);
    card->setObjectName("pageCard");
    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(16 * ssf, 12 * ssf, 16 * ssf, 14 * ssf);
    lay->setSpacing(6 * ssf);
    auto *label = new QLabel(title, card);
    label->setProperty("muted", true);
    lay->addWidget(label);
    return card;
  };

  // ---- left: current user and what each level may do
  auto *leftLay = new QVBoxLayout();
  leftLay->setSpacing(10 * ssf);
  mainLay->addLayout(leftLay, 1);

  auto *currentCard = makeCard(tr("Current user"));
  auto *currentLay  = static_cast<QVBoxLayout *>(currentCard->layout());
  m_currentName     = new QLabel(currentCard);
  {
    QFont font = m_currentName->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 2.0);
    m_currentName->setFont(font);
  }
  m_currentLevel = new QLabel(currentCard);
  m_loggedInSince = new QLabel(currentCard);
  m_loggedInSince->setProperty("muted", true);
  currentLay->addWidget(m_currentName);
  currentLay->addWidget(m_currentLevel, 0, Qt::AlignLeft);
  currentLay->addSpacing(6 * ssf);
  currentLay->addWidget(m_loggedInSince);
  currentLay->addStretch();
  leftLay->addWidget(currentCard, 2);

  auto *levelsCard = makeCard(tr("Access levels"));
  auto *levelsLay  = static_cast<QVBoxLayout *>(levelsCard->layout());
  QList<std::pair<int, QString>> const levels{
    {AccessManager::AccessLevel::SPECTATOR_0,
     tr("Home, Chart, Data analysis, AI (Simulator and Assistant), Users and System. Cannot start or stop the chamber.")},
    {AccessManager::AccessLevel::USER_START_1, tr("Everything above, plus Manual control and Program control.")},
    {AccessManager::AccessLevel::USER_EDIT_2, tr("As Level 1, and may run the AI Advisor, which changes the PID gains.")},
    {AccessManager::AccessLevel::USER_ADMIN_3, tr("Everything, including Settings.")},
  };
  for (auto const &[level, text] : levels) {
    levelsLay->addSpacing(8 * ssf);
    auto *name = new QLabel(levelName(level), levelsCard);
    QFont font = name->font();
    font.setBold(true);
    name->setFont(font);
    auto *description = new QLabel(text, levelsCard);
    description->setProperty("muted", true);
    description->setWordWrap(true);
    levelsLay->addWidget(name);
    levelsLay->addWidget(description);
  }
  levelsLay->addStretch();
  leftLay->addWidget(levelsCard, 3);

  // ---- right: user list
  auto *usersCard = makeCard(tr("Users"));
  auto *usersLay  = static_cast<QVBoxLayout *>(usersCard->layout());
  m_usersList     = new QTableWidget(usersCard);
  m_usersList->setColumnCount(2);
  m_usersList->setHorizontalHeaderLabels({tr("Name"), tr("Access level")});
  m_usersList->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_usersList->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  m_usersList->verticalHeader()->setVisible(false);
  m_usersList->verticalHeader()->setDefaultSectionSize(36 * ssf);
  m_usersList->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_usersList->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_usersList->setSelectionMode(QAbstractItemView::SingleSelection);
  m_usersList->setShowGrid(false);
  m_usersList->setFocusPolicy(Qt::NoFocus);
  usersLay->addWidget(m_usersList, 1);

  m_selectUserBtn = new QPushButton(tr("Выбрать"), usersCard);
  m_selectUserBtn->setObjectName("primaryButton");
  m_addUserBtn  = new QPushButton(tr("Добавить пользователя"), usersCard);
  m_editUserBtn = new QPushButton(tr("Редактировать"), usersCard);
  m_delUserBtn  = new QPushButton(tr("Удалить пользователя"), usersCard);
  m_delUserBtn->setProperty("outline", true);
  auto *buttonsLay = new QHBoxLayout();
  buttonsLay->setSpacing(8 * ssf);
  for (QPushButton *button : {m_selectUserBtn, m_addUserBtn, m_editUserBtn, m_delUserBtn}) {
    button->setFixedSize(110 * ssf, 34 * ssf);
    buttonsLay->addWidget(button);
  }
  buttonsLay->addStretch();
  auto *hint = new QLabel(tr("User changes are written to the log and recorded as experiment events."), usersCard);
  hint->setProperty("muted", true);
  buttonsLay->addWidget(hint);
  usersLay->addSpacing(6 * ssf);
  usersLay->addLayout(buttonsLay);
  mainLay->addWidget(usersCard, 2);

  m_editUserBtn->setEnabled(false);
  m_selectUserBtn->setEnabled(false);
  m_delUserBtn->setEnabled(false);

  m_since = QDateTime::currentDateTime();
  updateCurrentUser(m_accessManager->getCurrentUserName(), m_accessManager->getCurrentAccessLevel());
  connect(m_accessManager, &AccessManager::userSelected, this, [this](QString const &name, AccessManager::AccessLevel level) {
    m_since = QDateTime::currentDateTime();
    updateCurrentUser(name, level);
  });
}

void UserWidget::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if ((event->type() == QEvent::PaletteChange) && m_currentLevel) {
    updateCurrentUser(m_accessManager->getCurrentUserName(), m_accessManager->getCurrentAccessLevel());
  }
}

void UserWidget::updateCurrentUser(QString const &name, AccessManager::AccessLevel level) {
  theme::Colors const &c = theme::colors();
  m_currentName->setText(name);
  m_currentLevel->setText(levelName(level).toUpper());
  QColor const color = (level == AccessManager::AccessLevel::USER_ADMIN_3) ? c.warning : (level == AccessManager::AccessLevel::SPECTATOR_0 ? c.muted : c.ready);
  m_currentLevel->setStyleSheet(QStringLiteral("color: %1; border: %2px solid %1; border-radius: %3px; padding: %4px %5px; font-weight: bold;")
                                  .arg(color.name())
                                  .arg(qMax(1, static_cast<int>(1 * utils::ssf())))
                                  .arg(static_cast<int>(3 * utils::ssf()))
                                  .arg(static_cast<int>(1 * utils::ssf()))
                                  .arg(static_cast<int>(6 * utils::ssf())));
  QFont small = font();
  small.setPointSizeF(font().pointSizeF() * 0.8);
  m_currentLevel->setFont(small);
  m_loggedInSince->setText(tr("Logged in since %1").arg(m_since.toString("HH:mm")));
}

void UserWidget::updateBtnsEnable() {
  QString const selectedUserName{getSelectedUser()};
  auto const    selectedUserInfo{m_accessManager->userInfo(selectedUserName)};
  int const     selectedLevel{selectedUserInfo.second};

  QString const currentUserName{m_accessManager->getCurrentUserName()};
  int const     currentLevel{m_accessManager->getCurrentAccessLevel()};

  bool const selectedSomething{!selectedUserName.isEmpty()};

  m_selectUserBtn->setEnabled(selectedSomething && (selectedUserName != currentUserName));
  m_editUserBtn->setEnabled(selectedSomething && (selectedLevel <= currentLevel));
  m_delUserBtn->setEnabled(selectedSomething && (selectedUserName != currentUserName)
                           && (selectedLevel < currentLevel));

  m_addUserBtn->setEnabled(AccessManager::AccessLevel::SPECTATOR_0 != currentLevel);

  if ((AccessManager::AccessLevel::USER_ADMIN_3 == selectedLevel)
      || (AccessManager::SPECTATOR_USER_NAME == selectedUserName)) {
    m_editUserBtn->setEnabled(false);
    m_delUserBtn->setEnabled(false);
  }
}

void UserWidget::updateUsersList() {
  m_usersList->clearSelection();
  QStringList const names = m_accessManager->getUserNamesList();
  m_usersList->setRowCount(names.size());
  for (int row{0}; row < names.size(); ++row) {
    m_usersList->setItem(row, 0, new QTableWidgetItem(names[row]));
    m_usersList->setItem(row, 1, new QTableWidgetItem(levelName(m_accessManager->userInfo(names[row]).second)));
  }
  if ((m_usersList->currentRow() < 0) && (m_usersList->rowCount() > 0)) {
    m_usersList->selectRow(0);
  }
  updateBtnsEnable();
}

QString UserWidget::getSelectedUser() const {
  auto const selIndex = m_usersList->selectionModel()->selectedRows();
  int const  rowIndex{selIndex.isEmpty() ? -1 : selIndex.first().row()};
  if ((rowIndex < 0) || !m_usersList->item(rowIndex, 0)) {
    return {};
  }
  return m_usersList->item(rowIndex, 0)->text();
}

void UserWidget::switchUser() {
  QString const selectedUserName{getSelectedUser()};

  if (!selectedUserName.isEmpty() && (m_accessManager->getCurrentUserName() != selectedUserName)) {
    if (m_accessManager->getCurrentAccessLevel() < m_accessManager->userInfo(selectedUserName).second) {
      auto *keyboard = new VirtualKeyboard(tr("Проверка пароля"), VirtualKeyboard::EN, this);

      connect(keyboard, &VirtualKeyboard::textTyped, this, [this, selectedUserName](QString const &pass) {
        bool switchResult = m_accessManager->switchUser(selectedUserName, pass);
        if (!switchResult) {
          auto *msg = new MessageDialog(tr("Предупреждение"), tr("Введён неверный пароль"), this);
          msg->open();
        }
      });

      keyboard->open();
    } else {
      m_accessManager->switchUser(selectedUserName, QString{});
    }
  }
}
} // namespace deepvaccontrol
