#include "usereditdialog.h"

#include <QButtonGroup>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>

#include "messagedialog.h"
#include "utils.h"

namespace control2 {
AccessLevelWidget::AccessLevelWidget(QWidget *parent) {
  auto *mainLay = new QHBoxLayout(this);
  mainLay->setContentsMargins(0, 0, 0, 0);

  auto *box = new QGroupBox(tr("Уровень доступа"), this);
  box->setContentsMargins(0, 0, 0, 0);

  auto *labelsLay = new QVBoxLayout(box);
  mainLay->addWidget(box);

  m_btns = new QButtonGroup(this);

  m_labels = {tr("Наблюдатель"), tr("Уровень 1"), tr("Уровень 2"), tr("Сервисный инженер")};
  for (int i{0U}; i < m_labels.size(); ++i) {
    auto *btn = new QRadioButton(m_labels.at(i), this);
    btn->setStyleSheet(utils::readStyleFromFile(":/styles/radioBtnBig.qss")
                       + QString("QRadioButton::indicator{width: %1 px; height: %1 px;}").arg(20 * utils::ssf()));
    m_radios << btn;
    m_btns->addButton(btn, i);
    labelsLay->addWidget(btn);
  }

  labelsLay->addStretch();

  setLevel(0);
}

void AccessLevelWidget::setMaxLevel(int maxLevel) {
  for (int i{0}; i < m_labels.size(); ++i) {
    m_radios[i]->setEnabled(i <= maxLevel);
  }
}

void AccessLevelWidget::setLevel(int level) {
  m_btns->button(level)->setChecked(true);
}

int AccessLevelWidget::getLevel() const {
  return m_btns->checkedId();
}

UserEditDialog::UserEditDialog(AccessManager *accessManager, QString const &editName, QWidget *parent)
    : QDialog{parent}, m_accessManager{accessManager}, m_addNew{editName.isEmpty()} {
  initGui();

  int level{-1};
  if (m_addNew) {
    m_nameLine->setText(generateNewName());
    level = m_accessManager->getCurrentAccessLevel() - 1;
  } else {
    m_nameLine->setText(editName);
    m_nameLine->setEnabled(false);
    level = m_accessManager->userInfo(editName).second;
    m_levelEdit->setEnabled(false);
  }
  m_levelEdit->setLevel(level);
  m_levelEdit->setMaxLevel(m_accessManager->getCurrentAccessLevel());
}

void UserEditDialog::initGui() {
  auto *mainLay = new QVBoxLayout(this);

  auto *formLay = new QFormLayout();
  m_nameLine    = new LineEditScreen(this);
  m_passLine    = new LineEditScreen(this);
  m_levelEdit   = new AccessLevelWidget(this);

  auto *btnLay    = new QHBoxLayout();
  auto *cancelBtn = new QPushButton(tr("Отмена"), this);
  m_saveBtn       = new QPushButton(tr("Сохранить"), this);

  btnLay->addStretch();
  btnLay->addWidget(m_saveBtn);
  btnLay->addWidget(cancelBtn);

  formLay->addRow(tr("Имя пользователя"), m_nameLine);
  formLay->addRow(tr("Пароль"), m_passLine);

  mainLay->addLayout(formLay);
  mainLay->addWidget(m_levelEdit);
  mainLay->addLayout(btnLay);

  connect(cancelBtn, &QPushButton::clicked, this, &UserEditDialog::reject);
  connect(m_saveBtn, &QPushButton::clicked, this, &UserEditDialog::saveHandle);
  connect(m_nameLine, &LineEditScreen::textChanged, this, [this](QString const &text) {
    m_saveBtn->setEnabled(!text.isEmpty());
  });

  {
    auto font = m_nameLine->font();
    font.setPointSize(font.pointSize() * 1.5);
    for (QWidget *item : QWidgetList({m_nameLine,
                                      m_passLine,
                                      m_levelEdit,
                                      m_saveBtn,
                                      cancelBtn,
                                      formLay->labelForField(m_nameLine),
                                      formLay->labelForField(m_passLine)})) {
      item->setFont(font);
    }
  }

  cancelBtn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());
  m_saveBtn->setFixedSize(100 * utils::ssf(), 50 * utils::ssf());

  setWindowTitle(m_addNew ? tr("Добавление нового пользователя") : tr("Редактирование пользователя"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlag(Qt::WindowStaysOnTopHint);
  adjustSize();
  setFixedSize(this->size());
}

void UserEditDialog::saveHandle() {
  QString const name{m_nameLine->text()};
  QString const pass{m_passLine->text()};
  int const     level{m_levelEdit->getLevel()};

  if (m_accessManager->getUserNamesList().contains(name) && m_addNew) {
    auto *msg
      = new MessageDialog(tr("Ошибка"), tr("Пользователь с таким именеим уже существует.\nИзмените имя."), this);
    msg->open();
  } else if (pass.isEmpty()) {
    auto *msg = new MessageDialog(tr("Ошибка"), tr("Введите новый пароль"), this);
    msg->open();
  } else if (!m_addNew) {
    if (m_accessManager->changePassword(name, pass)) {
      auto *msg = new MessageDialog(tr("Подтверждение"), tr("Пользователь успешно изменён"), this);
      connect(msg, &MessageDialog::finished, this, [this]() {
        UserEditDialog::accept();
      });
      msg->open();
    }
  } else if (m_accessManager->addUser(name, static_cast<AccessManager::AccessLevel>(level), pass)) {
    auto *msg = new MessageDialog(tr("Подтверждение"), tr("Пользователь успешно добавлен"), this);
    connect(msg, &MessageDialog::finished, this, [this]() {
      UserEditDialog::accept();
    });
    msg->open();
  }
}

QString UserEditDialog::generateNewName() {
  QStringList const names{m_accessManager->getUserNamesList()};

  int i{1};
  while (names.contains(QString(tr("Пользователь %1")).arg(i))) {
    ++i;
  }
  return QString(tr("Пользователь %1")).arg(i);
}

} // namespace control2
