#ifndef ACCESSMANAGER_H
#define ACCESSMANAGER_H

#include <QCryptographicHash>
#include <QMap>
#include <QObject>
#include <QRandomGenerator>

#include "filehandler.h"
#include "logger.h"

namespace control2 {

class ResHolder;

class AccessManager
    : public QObject
    , public FileHandler {
  Q_OBJECT
  inline static QString const LAST_USER_FILE{"last_user.txt"};
  inline static QString const SECRET_KEY{"SuperSecretKey123"};
  inline static QString const ADMIN_USER_NAME{QObject::tr("System Engineer")}; //Системный инженер
  inline static QString const ADMIN_DEFAULT_PASS{"8520"};

public:
  inline static QString const SPECTATOR_USER_NAME{QObject::tr("Observer")}; // Наблюдатель

  enum AccessLevel {
    SPECTATOR_0 = 0,
    USER_START_1,
    USER_EDIT_2,
    USER_ADMIN_3
  };
  AccessManager(ResHolder *resHolder, Logger *logger, QObject *parent = nullptr);
  ~AccessManager();
  bool addUser(QString const &username, AccessLevel accessLevel, QString const &password);
  bool changePassword(QString const &username, QString const &newPassword);
  bool switchUser(QString const &username, QString const &password = "");
  bool removeUser(QString const &username);

  AccessLevel                 getCurrentAccessLevel() const;
  QString                     getCurrentUserName() const;
  QStringList                 getUserNamesList() const;
  QPair<QString, AccessLevel> userInfo(QString const &username) const;

signals:
  void userSelected(QString name, AccessLevel level);
  void userListChanged();

private:
  struct User final {
  public:
    User() = default;
    explicit User(AccessLevel level, QString const &salt, QString const &hasedPass);
    explicit User(QJsonObject const &json);
    QJsonObject toJson() const;

    AccessLevel m_accessLevel{AccessLevel::SPECTATOR_0};
    QString     m_salt;
    QString     m_hashedPassword;
  };

  void switchUserImpl(QString const &username, bool shadow = false);

  QString generateSalt();

  QString hashPassword(QString const &password, QString const &salt);

  bool    verifyPassword(QString const &username, QString const &password);
  // Генерация подписи для пользователя
  QString generateSignature(QString const &username);

  // Проверка подписи
  bool verifySignature(QString const &username, QString const &signature);

  void loadUsers();
  void saveUsers();

  ResHolder          *m_resHolder;
  Logger             *m_logger;
  QMap<QString, User> m_users;
  QString             m_currentUser;
  QString             m_lastUserFromFile;
  AccessLevel         m_currentAccessLevel;
  // Секретный ключ для подписи (его нужно хранить в программе или в защищённой области)

  // FileHandler interface
protected:
  bool writeData(QFile &file) override;
  bool readData(QFile &file) override;
};

inline AccessManager::AccessLevel AccessManager::getCurrentAccessLevel() const {
  return m_currentAccessLevel;
}

inline QString AccessManager::getCurrentUserName() const {
  return m_currentUser;
}

inline QStringList AccessManager::getUserNamesList() const {
  return m_users.keys();
}
} // namespace control2

#endif // ACCESSMANAGER_H
