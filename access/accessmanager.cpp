#include "accessmanager.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "constants.h"
#include "resholder.h"
#include "stringpathprovider.h"

namespace deepvaccontrol {
AccessManager::AccessManager(ResHolder *resHolder, Logger *logger, QObject *parent)
    : QObject(parent), FileHandler{}, m_resHolder{resHolder}, m_logger{logger} {
  loadUsers();

  if (!m_users.contains(SPECTATOR_USER_NAME)) {
    m_logger->stream(Logger::Level::WAR) << QString("There is no default user '%1'. Add it.").arg(SPECTATOR_USER_NAME);
    addUser(SPECTATOR_USER_NAME, AccessLevel::SPECTATOR_0, "");
  }
  if (!m_users.contains(ADMIN_USER_NAME)) {
    m_logger->stream(Logger::Level::WAR) << QString("There is no default user '%1'. Add it.").arg(ADMIN_USER_NAME);
    addUser(ADMIN_USER_NAME, AccessLevel::USER_ADMIN_3, ADMIN_DEFAULT_PASS);
  }

  if (m_users.contains(m_lastUserFromFile)) {
    switchUserImpl(m_lastUserFromFile, /*shadow*/ true); // Attack could be done here!!!
  } else {
    m_logger->stream(Logger::Level::ERR) << QString("There is no user '%1'").arg(m_lastUserFromFile);
    switchUserImpl(SPECTATOR_USER_NAME, /*shadow*/ true);
  }
}

AccessManager::~AccessManager() {
  try {
    saveUsers();
  } catch (...) {
    m_logger->stream(Logger::Level::ERR) << "Ooops!";
  }
}

bool AccessManager::addUser(QString const &username, AccessLevel accessLevel, QString const &password) {
  if (m_users.contains(username)) {
    m_logger->stream(Logger::Level::WAR) << QString("User %1 already exists").arg(username);
    return false;
  }
  if ((accessLevel < 0) || (accessLevel > 3)) {
    m_logger->stream(Logger::Level::WAR) << "Unknown access level";
    return false;
  }

  QString const salt{generateSalt()};
  QString const hashedPassword{hashPassword(password, salt)};
  m_users[username] = User{accessLevel, salt, hashedPassword};
  m_resHolder->actions->addAction(ActionManager::ActionType::APP,
                                  QString(tr("Пользователь '%1' добавлен")).arg(username));
  emit userListChanged();
  return true;
}

bool AccessManager::changePassword(QString const &username, QString const &newPassword) {
  User const oldUser = m_users[username];

  QString const salt{generateSalt()};
  QString const hashedPassword{hashPassword(newPassword, salt)};
  m_users[username] = User{oldUser.m_accessLevel, salt, hashedPassword};
  return true;
}

bool AccessManager::switchUser(QString const &username, QString const &password) {
  if (!m_users.contains(username)) {
    m_logger->stream(Logger::Level::ERR) << QString("There is no user '%1'").arg(username);
    return false;
  }

  User const &targetUser{m_users[username]};
  if ((m_currentAccessLevel >= targetUser.m_accessLevel) || (verifyPassword(username, password))) {
    switchUserImpl(username);
    return true;
  } else {
    m_logger->stream(Logger::Level::INFO) << QString("Wrong password for user '%1'").arg(username);
    return false;
  }
}

bool AccessManager::removeUser(QString const &username) {
  if (!m_users.contains(username)) {
    m_logger->stream(Logger::Level::ERR) << QString("There is no user '%1'").arg(username);
    return false;
  }

  m_users.remove(username);
  m_resHolder->actions->addAction(ActionManager::ActionType::APP, QString(tr("Пользователь '%1' удалён")).arg(username));
  emit userListChanged();
  return true;
}

QPair<QString, AccessManager::AccessLevel> AccessManager::userInfo(QString const &username) const {
  if (!m_users.contains(username)) {
    m_logger->stream(Logger::Level::ERR) << QString("There is no user '%1'").arg(username);
    return {};
  }

  return {username, m_users[username].m_accessLevel};
}

void AccessManager::switchUserImpl(QString const &username, bool shadow) {
  m_currentUser        = username;
  m_currentAccessLevel = m_users[username].m_accessLevel;
  emit userSelected(m_currentUser, m_currentAccessLevel);
  m_logger->stream(Logger::Level::INFO)
    << QString("User '%1' with level %2 selected").arg(m_currentUser).arg(m_currentAccessLevel);
  if (!shadow) {
    m_resHolder->actions->addAction(ActionManager::ActionType::APP,
                                    QString(tr("Выбран пользователь '%1'")).arg(m_currentUser));
  }
}

QString AccessManager::generateSalt() {
  return QString::number(QRandomGenerator::global()->generate()); // Простая случайная соль
}

QString AccessManager::hashPassword(QString const &password, QString const &salt) {
  QByteArray const saltedPassword{password.toUtf8() + salt.toUtf8()};
  QByteArray const hash{QCryptographicHash::hash(saltedPassword, QCryptographicHash::Sha256)};
  return QString(hash.toHex());
}

bool AccessManager::verifyPassword(QString const &username, QString const &password) {
  if (!m_users.contains(username)) {
    return false;
  }
  User const   &user{m_users[username]};
  QString const hashedInputPassword{hashPassword(password, user.m_salt)};
  return hashedInputPassword == user.m_hashedPassword;
}

QString AccessManager::generateSignature(QString const &username) {
  QByteArray data = username.toUtf8() + SECRET_KEY.toUtf8();
  QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
  return QString(hash.toHex());
}

bool AccessManager::verifySignature(QString const &username, QString const &signature) {
  return generateSignature(username) == signature;
}

void AccessManager::loadUsers() {
  StringPathProvider path(deepvaccontrol::USER_FILE, m_logger, this);
  FileHandler::loadFromFile(&path);
}

void AccessManager::saveUsers() {
  StringPathProvider path(deepvaccontrol::USER_FILE, m_logger, this);
  FileHandler::saveToFile(&path);
}

bool AccessManager::writeData(QFile &file) {
  QJsonObject root;

  QJsonObject usersJson;
  for (auto it = m_users.begin(); it != m_users.end(); ++it) {
    usersJson[it.key()] = it.value().toJson();
  }
  root["users"]    = usersJson;
  root["lastUser"] = m_currentUser;

  QJsonDocument doc(root);
  file.write(doc.toJson());
  return true;
}

bool AccessManager::readData(QFile &file) {
  QByteArray const    data{file.readAll()};
  QJsonDocument const doc{QJsonDocument::fromJson(data)};

  if (!doc.isObject()) {
    m_logger->stream(Logger::Level::ERR) << "Incorrect data format";
    return false;
  }

  QJsonObject const root{doc.object()};
  QJsonObject const usersJson{root["users"].toObject()};

  m_users.clear();
  for (auto it = usersJson.begin(); it != usersJson.end(); ++it) {
    m_users[it.key()] = User(it.value().toObject());
  }

  m_lastUserFromFile = root["lastUser"].toString();

  return true;
}

AccessManager::User::User(QJsonObject const &json) {
  m_accessLevel    = static_cast<AccessLevel>(json["accessLevel"].toInt());
  m_salt           = json["salt"].toString();
  m_hashedPassword = json["hashedPassword"].toString();
}

AccessManager::User::User(AccessLevel level, QString const &salt, QString const &hasedPass)
    : m_accessLevel{level}, m_salt{salt}, m_hashedPassword{hasedPass} {
}

QJsonObject AccessManager::User::toJson() const {
  QJsonObject json;
  json["accessLevel"]    = m_accessLevel;
  json["salt"]           = m_salt;
  json["hashedPassword"] = m_hashedPassword;
  return json;
}

} // namespace deepvaccontrol
