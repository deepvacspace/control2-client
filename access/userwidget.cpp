#include "userwidget.h"

#include <QLabel>
#include <QVBoxLayout>

#include "constants.h"
#include "messagedialog.h"
#include "usereditdialog.h"
#include "utils.h"
#include "virtualkeyboard.h"

namespace control2 {
UserWidget::UserWidget(Logger *logger, AccessManager *accessManager, QWidget *parent)
    : QWidget{parent}, m_logger{logger}, m_accessManager{accessManager} {
  initGui();

  connect(m_addUserBtn, &QPushButton::clicked, this, [this] {
    auto editDialog = new UserEditDialog(m_accessManager, "", this);
    editDialog->open();
  });

  connect(m_editUserBtn, &QPushButton::clicked, this, [this] {
    auto editDialog = new UserEditDialog(m_accessManager, m_usersList->currentItem()->text(), this);
    editDialog->open();
  });

  connect(m_delUserBtn, &QPushButton::clicked, this, [this]() {
    QString const selectedUser{getSelectedUser()};
    if (!selectedUser.isEmpty()) {
      m_accessManager->removeUser(selectedUser);
    }
  });

  connect(m_selectUserBtn, &QPushButton::clicked, this, &UserWidget::switchUser);
  connect(m_usersList, &QListWidget::doubleClicked, this, &UserWidget::switchUser);
  connect(m_usersList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &UserWidget::updateBtnsEnable);
  connect(m_accessManager, &AccessManager::userListChanged, this, &UserWidget::updateUsersList);
  connect(m_accessManager, &AccessManager::userSelected, this, &UserWidget::updateBtnsEnable);

  updateUsersList();
}

void UserWidget::initGui() {
  // current / midLay
  auto *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(3 * utils::ssf(), 10 * utils::ssf(), 0, 0);
  auto *currentUserLabel
    = new QLabel(QString(tr("Текущий пользователь: %1")).arg(m_accessManager->getCurrentUserName()), this);

  // list | btnLay
  auto *midLay = new QHBoxLayout();
  midLay->setContentsMargins(0, 0, 0, 0);
  midLay->setSpacing(3 * utils::ssf());

  m_usersList   = new QListWidget(this);
  auto *btnsLay = new QVBoxLayout();
  btnsLay->setSpacing(3 * utils::ssf());
  btnsLay->setContentsMargins(0, 0, 0, 0);
  m_selectUserBtn = new QPushButton(tr("Выбрать"), this);
  m_editUserBtn   = new QPushButton(tr("Редактировать"), this);
  m_addUserBtn    = new QPushButton(tr("Добавить пользователя"), this);
  m_delUserBtn    = new QPushButton(tr("Удалить пользователя"), this);

  m_selectUserBtn->setFixedHeight(150 * utils::ssf());
  m_editUserBtn->setFixedHeight(control2::UI_BTN_HEIGHT * utils::ssf());
  m_addUserBtn->setFixedHeight(control2::UI_BTN_HEIGHT * utils::ssf());
  m_delUserBtn->setFixedHeight(control2::UI_BTN_HEIGHT * utils::ssf());

  btnsLay->addWidget(m_selectUserBtn);
  btnsLay->addWidget(m_editUserBtn);
  btnsLay->addWidget(m_addUserBtn);
  btnsLay->addWidget(m_delUserBtn);
  btnsLay->addStretch();

  midLay->addWidget(m_usersList);
  midLay->addLayout(btnsLay);

  mainLay->addWidget(currentUserLabel);
  mainLay->addLayout(midLay);

  m_editUserBtn->setEnabled(false);
  m_selectUserBtn->setEnabled(false);
  m_delUserBtn->setEnabled(false);

  m_usersList->setStyleSheet(QString(utils::readStyleFromFile(":/styles/accessUserList.qss")).arg(30 * utils::ssf()));
  {
    auto font = currentUserLabel->font();
    font.setPointSize(font.pointSize() * 1.5);
    for (QWidget *item : QWidgetList({m_addUserBtn, m_editUserBtn, m_delUserBtn, currentUserLabel, m_usersList})) {
      item->setFont(font);
    }

    font.setPointSize(font.pointSize() * 1.5);
    m_selectUserBtn->setFont(font);
  }

  connect(m_accessManager,
          &AccessManager::userSelected,
          this,
          [currentUserLabel](QString const &name, AccessManager::AccessLevel) {
            currentUserLabel->setText(QString(tr("Текущий пользователь: %1")).arg(name));
          });
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
  m_usersList->clear();
  m_usersList->addItems(m_accessManager->getUserNamesList());
  if ((m_usersList->currentRow() < 0) && (m_usersList->count() > 0)) {
    m_usersList->setCurrentRow(0);
  }
  updateBtnsEnable();
}

QString UserWidget::getSelectedUser() const {
  auto const selIndex = m_usersList->selectionModel()->selectedRows();
  int const  rowIndex{selIndex.isEmpty() ? -1 : selIndex.first().row()};
  bool       selectedSomething{true};
  if (rowIndex < 0) {
    qDebug() << "===================================== ERROR ================";
    return {};
  }
  return m_usersList->item(rowIndex)->text();
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
} // namespace control2
